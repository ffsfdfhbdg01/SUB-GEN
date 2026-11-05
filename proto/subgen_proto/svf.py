# SPDX-License-Identifier: AGPL-3.0-or-later
"""TPT / Cytomic state-variable filter sections and the cascades built from them.

A `Section` realises y = m0*v0 + m1*v1 + m2*v2 where v0 is the input, v1 the band-pass tap and v2 the
low-pass tap of a trapezoidal-integrator SVF (Simper 2013). With s' = j*tan(w/2)/g and
D = s'^2 + k*s' + 1:  v2/v0 = 1/D  and  v1/v0 = s'/D  (derived by hand, checked against the time-domain loop).
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import signal as sps


@dataclass(frozen=True)
class Section:
    g: float
    k: float
    m0: float
    m1: float
    m2: float


@dataclass(frozen=True)
class FirstOrder:
    g: float
    kind: str  # 'low' | 'high'


def lowpass(g: float, k: float) -> Section:
    return Section(g, k, 0.0, 0.0, 1.0)


def highpass(g: float, k: float) -> Section:
    return Section(g, k, 1.0, -k, -1.0)


# ---------------------------------------------------------------------------------------------- designs
def _butter_qs(order: int):
    # Butterworth poles sit at -sin(t) + j*cos(t), t = (2i-1)*pi/(2n), so Q = 1/(2 sin t). For even n the set
    # equals the 1/(2 cos t) form quoted in SPEC §7.6; for odd n only the sin form is right (n = 3 -> Q = 1).
    qs = [1.0 / (2.0 * np.sin((2 * i - 1) * np.pi / (2 * order))) for i in range(1, order // 2 + 1)]
    return qs, order % 2 == 1


def design_butter(order: int, fc: float, fs: float, kind: str) -> list:
    """Butterworth of any order >= 1. Every section uses g = tan(pi*fc/fs) (single prewarp, as SciPy)."""
    g = float(np.tan(np.pi * fc / fs))
    qs, odd = _butter_qs(order)
    make = lowpass if kind == "low" else highpass
    secs: list = [make(g, 1.0 / q) for q in qs]
    if odd:
        secs.append(FirstOrder(g, kind))
    return secs


def design_lr(order: int, fc: float, fs: float, kind: str) -> list:
    """Linkwitz-Riley of even order = two cascaded Butterworth filters of half the order."""
    if order < 2 or order % 2:
        raise ValueError("Linkwitz-Riley order must be even and >= 2")
    half = design_butter(order // 2, fc, fs, kind)
    return half + half


def design_bessel(order: int, fc: float, fs: float, kind: str) -> list:
    """Bessel (-3 dB at fc) for orders 2, 4, 8. Section natural frequency w0_i comes from the poles of
    scipy.signal.bessel(norm='mag'); LP g = w0_i*tan(pi fc/fs), HP g = tan(pi fc/fs)/w0_i (lp2hp)."""
    if order not in (2, 4, 8):
        raise ValueError("Bessel order must be 2, 4 or 8")
    _, poles, _ = sps.bessel(order, 1.0, btype="low", analog=True, output="zpk", norm="mag")
    t = float(np.tan(np.pi * fc / fs))
    secs = []
    for p in sorted((p for p in poles if p.imag > 0), key=abs):
        w0 = float(abs(p))
        k = float(2.0 * (-p.real) / w0)  # 1/Q
        secs.append(lowpass(w0 * t, k) if kind == "low" else highpass(t / w0, k))
    return secs


def cytomic(kind: str, fc: float, q: float, gain_db: float, fs: float) -> Section:
    """Bell / low shelf / high shelf (Simper's SvfLinearTrapOptimised2 mixing coefficients)."""
    a = 10.0 ** (gain_db / 40.0)
    t = float(np.tan(np.pi * fc / fs))
    if kind == "bell":
        k = 1.0 / (q * a)
        return Section(t, k, 1.0, k * (a * a - 1.0), 0.0)
    k = 1.0 / q
    if kind == "lowshelf":
        return Section(t / np.sqrt(a), k, 1.0, k * (a - 1.0), a * a - 1.0)
    if kind == "highshelf":
        return Section(t * np.sqrt(a), k, a * a, k * (1.0 - a) * a, 1.0 - a * a)
    raise ValueError(f"unknown shape {kind!r}")


# -------------------------------------------------------------------------------------------- processing
def _run_section(sec: Section, x: np.ndarray) -> np.ndarray:
    g, k = sec.g, sec.k
    a1 = 1.0 / (1.0 + g * (g + k))
    a2 = g * a1
    a3 = g * a2
    m0, m1, m2 = sec.m0, sec.m1, sec.m2
    ic1 = ic2 = 0.0
    out = [0.0] * len(x)
    for n, v0 in enumerate(x.tolist()):
        v3 = v0 - ic2
        v1 = a1 * ic1 + a2 * v3
        v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2.0 * v1 - ic1
        ic2 = 2.0 * v2 - ic2
        out[n] = m0 * v0 + m1 * v1 + m2 * v2
    return np.array(out)


def _run_first_order(sec: FirstOrder, x: np.ndarray) -> np.ndarray:
    c = sec.g / (1.0 + sec.g)
    low = sec.kind == "low"
    s = 0.0
    out = [0.0] * len(x)
    for n, v0 in enumerate(x.tolist()):
        v = (v0 - s) * c
        lp = v + s
        s = lp + v
        out[n] = lp if low else v0 - lp
    return np.array(out)


def process(sections: list, x) -> np.ndarray:
    """Reference implementation: the exact TPT state-update loop, one section after another, in float64."""
    y = np.asarray(x, dtype=np.float64)
    for sec in sections:
        y = _run_first_order(sec, y) if isinstance(sec, FirstOrder) else _run_section(sec, y)
    return y


def to_sos(sections: list) -> np.ndarray:
    """Equivalent biquad rows [b0 b1 b2 1 a1 a2] of a cascade (bilinear form of the TPT section)."""
    rows = []
    for sec in sections:
        if isinstance(sec, FirstOrder):
            g = sec.g
            b0 = (g if sec.kind == "low" else 1.0) / (g + 1.0)
            b1 = (g if sec.kind == "low" else -1.0) / (g + 1.0)
            rows.append([b0, b1, 0.0, 1.0, (g - 1.0) / (g + 1.0), 0.0])
            continue
        g, k = sec.g, sec.k
        a0 = 1.0 + g * k + g * g
        b0 = sec.m0 * a0 + sec.m1 * g + sec.m2 * g * g
        b1 = sec.m0 * (-2.0 + 2.0 * g * g) + 2.0 * sec.m2 * g * g
        b2 = sec.m0 * (1.0 - g * k + g * g) - sec.m1 * g + sec.m2 * g * g
        rows.append([b0 / a0, b1 / a0, b2 / a0, 1.0, (-2.0 + 2.0 * g * g) / a0, (1.0 - g * k + g * g) / a0])
    return np.array(rows)


def process_fast(sections: list, x) -> np.ndarray:
    """Same transfer function as `process` through SciPy's biquad filter (static coefficients only)."""
    return sps.sosfilt(to_sos(sections), np.asarray(x, dtype=np.float64))


def process_varying(x, g, k: float, mode: str = "low") -> np.ndarray:
    """One TPT SVF with a per-sample g (cutoff modulation). mode: 'low' | 'band' | 'high'."""
    x = np.asarray(x, dtype=np.float64)
    g = np.asarray(g, dtype=np.float64)
    a1 = (1.0 / (1.0 + g * (g + k))).tolist()
    a2 = (g * np.asarray(a1)).tolist()
    a3 = (g * np.asarray(a2)).tolist()
    ic1 = ic2 = 0.0
    out = [0.0] * len(x)
    for n, v0 in enumerate(x.tolist()):
        v3 = v0 - ic2
        v1 = a1[n] * ic1 + a2[n] * v3
        v2 = ic2 + a2[n] * ic1 + a3[n] * v3
        ic1 = 2.0 * v1 - ic1
        ic2 = 2.0 * v2 - ic2
        out[n] = v2 if mode == "low" else v1 if mode == "band" else v0 - k * v1 - v2
    return np.array(out)


# -------------------------------------------------------------------------------------- analytic response
def freqz(sections: list, freqs_hz, fs: float) -> np.ndarray:
    """Complex frequency response of the digital cascade, from the section coefficients."""
    sigma = 1j * np.tan(np.pi * np.asarray(freqs_hz, dtype=np.float64) / fs)  # (z-1)/(z+1) on the unit circle
    h = np.ones(len(sigma), dtype=np.complex128)
    for sec in sections:
        sp = sigma / sec.g
        if isinstance(sec, FirstOrder):
            h *= 1.0 / (1.0 + sp) if sec.kind == "low" else sp / (1.0 + sp)
        else:
            d = sp * sp + sec.k * sp + 1.0
            h *= (sec.m0 * d + sec.m1 * sp + sec.m2) / d
    return h


def group_delay_s(sections: list, freqs_hz, fs: float) -> np.ndarray:
    """Group delay in seconds: -dphi/domega by a centred difference of the phase of H(f+h)/H(f-h)."""
    f = np.asarray(freqs_hz, dtype=np.float64)
    step = f * 1e-5 + 1e-9
    ratio = freqz(sections, f + step, fs) / freqz(sections, f - step, fs)
    return -np.angle(ratio) / (2.0 * np.pi * 2.0 * step)
