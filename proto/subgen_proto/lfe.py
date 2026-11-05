# SPDX-License-Identifier: AGPL-3.0-or-later
"""LFE filter analysis and the linear-phase FIR (SPEC §7.6, SPEC_IO_UI §4).

encoder_filter_sos      the 8th-order elliptic 120 Hz low-pass that ATSC A/52 puts in the LFE encoder. A/52 gives the
                        order and the corner only; the 0.1 dB passband ripple and 80 dB stopband are an ASSUMPTION
out_of_band_db          attenuation of a plug-in low-pass at ratio x its corner
linear_phase_fir        symmetric FIR whose AMPLITUDE response follows `target_mag(f)` (the prototype's |H|, not |H|^2)
min_taps                shortest kernel that follows the target within a tolerance
"""
from __future__ import annotations

import numpy as np
from scipy import signal as sps

from . import svf

ENCODER_FC, ENCODER_RIPPLE_DB, ENCODER_STOP_DB = 120.0, 0.1, 80.0
DEFAULT_BETA = 3.0  # Kaiser: the main-lobe smear of the 20 Hz corner is smallest for beta ~ 2-3 (SPEC says 8: 0.19 dB at 16384 taps)
EVAL_FFT = 2 ** 19


def encoder_filter_sos(fs: float) -> np.ndarray:
    return sps.ellip(8, ENCODER_RIPPLE_DB, ENCODER_STOP_DB, ENCODER_FC, btype="low", fs=fs, output="sos")


def group_delay_sos_s(sos, f_hz, fs: float) -> np.ndarray:
    """Group delay (s) of an SOS cascade: the sum of its biquads' delays."""
    f = np.asarray(f_hz, dtype=float)
    total = np.zeros_like(f)
    for row in np.atleast_2d(sos):
        total += sps.group_delay((row[:3], row[3:]), w=f, fs=fs)[1] / fs
    return total


def out_of_band_db(kind: str, order: int, fc: float, fs: float, ratio: float = 1.5) -> float:
    """Attenuation (dB, positive) of the plug-in low-pass `kind` ('butter' | 'lr' | 'bessel') of the given order at ratio * fc."""
    design = {"butter": svf.design_butter, "lr": svf.design_lr, "bessel": svf.design_bessel}[kind]
    return float(-20.0 * np.log10(np.abs(svf.freqz(design(order, fc, fs, "low"), [ratio * fc], fs))[0]))


def linear_phase_fir(target_mag, fs: float, n_taps: int, beta: float = DEFAULT_BETA) -> np.ndarray:
    """Kernel h (n_taps, exactly symmetric, delay (n_taps - 1)/2 samples) with |H(f)| ~ target_mag(f): the zero-phase
    response is sampled on a grid of >= 4 n_taps points, delayed by (n_taps - 1)/2 and windowed with Kaiser(beta)."""
    m = 4 * 2 ** int(np.ceil(np.log2(n_taps)))
    f = np.fft.rfftfreq(m, 1.0 / fs)
    h = np.fft.irfft(target_mag(f) * np.exp(-1j * np.pi * f * (n_taps - 1) / fs), m)[:n_taps] * np.kaiser(n_taps, beta)
    return 0.5 * (h + h[::-1])


def fir_error_db(h, target_mag, fs: float, f_check=(25.0, 2000.0)) -> float:
    """Largest |amplitude error| (dB) of the kernel against the target between f_check[0] and f_check[1] Hz."""
    f = np.fft.rfftfreq(EVAL_FFT, 1.0 / fs)
    sel = (f >= f_check[0]) & (f <= f_check[1])
    return float(np.max(np.abs(20.0 * np.log10(np.abs(np.fft.rfft(h, EVAL_FFT))[sel] / target_mag(f[sel])))))


def min_taps(target_mag, fs: float, tol_db: float = 0.1, f_check=(25.0, 2000.0), beta: float = DEFAULT_BETA,
             n_max: int = 262144) -> int:
    """Smallest kernel length whose amplitude error stays within tol_db over f_check (doubling, then bisection)."""
    meets = lambda n: fir_error_db(linear_phase_fir(target_mag, fs, n, beta), target_mag, fs, f_check) <= tol_db
    hi = 256
    while not meets(hi):
        hi *= 2
        if hi > n_max:
            raise ValueError(f"no kernel up to {n_max} taps reaches {tol_db} dB")
    lo = hi // 2 if hi > 256 else 2
    while hi - lo > 1:
        mid = (lo + hi) // 2
        lo, hi = (lo, mid) if meets(mid) else (mid, hi)
    return hi
