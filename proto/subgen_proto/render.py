# SPDX-License-Identifier: AGPL-3.0-or-later
"""Rendering helpers for the listening files (P2-11) and render_file: A-weighted loudness matching, 24-bit WAV
writing, the 5.1 LFE layout, and engine wrappers that turn a source band x_b into a sub.

TRACK in Mix/Print: the lookahead serves only the pitch and confidence (known L_look early, so the gate is open and
the pitch right at the onset); the amplitude follows the source as it happens, so the sub never starts before the dry.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal as sps

from . import divide, harmonic, svf, track, trigger, util

ENGINES = ("divide", "track", "trigger", "harmonic")


def loudness_db(x, fs: float) -> float:
    """A-weighted RMS level (dB) of a mono (n,) or multichannel (n, ch) signal; the first 100 ms are skipped."""
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        return util.loudness_a_db(x, fs)
    y = sps.sosfilt(util.a_weight_sos(fs), x, axis=0)
    skip = min(len(y) // 10, int(0.1 * fs))
    return float(10.0 * np.log10(max(float(np.mean(y[skip:] ** 2)), 1e-300)))


def gain_match(ref, test, fs: float):
    """Scale `test` to the A-weighted loudness of `ref`: returns (test_matched, gain_db)."""
    gain_db = loudness_db(ref, fs) - loudness_db(test, fs)
    return np.asarray(test, dtype=np.float64) * 10.0 ** (gain_db / 20.0), gain_db


def write_wav(path, data, fs: float) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(path, np.asarray(data, dtype=np.float64), int(fs), subtype="PCM_24")
    return path


def write_ab(dirpath, name: str, dry, wet, fs: float) -> list:
    """<name>_A_dry.wav and <name>_B_<label>.wav for every variant in `wet` (a dict label -> signal, or one signal
    labelled 'wet'), each matched to the dry's A-weighted loudness; one common scale keeps all of them below -0.2 dBFS."""
    variants = wet if isinstance(wet, dict) else {"wet": wet}
    files = {f"{name}_A_dry.wav": np.asarray(dry, dtype=np.float64)}
    for label, sig in variants.items():
        files[f"{name}_B_{label}.wav"] = gain_match(dry, sig, fs)[0]
    peak = max(float(np.max(np.abs(v))) for v in files.values())
    scale = min(1.0, 10.0 ** (-0.2 / 20.0) / peak) if peak > 0.0 else 1.0
    return [write_wav(Path(dirpath) / fname, sig * scale, fs) for fname, sig in files.items()]


def lfe_51(front_lr, lfe, fs: float, *, lp_hz: float = 120.0, order: int = 4) -> np.ndarray:
    """(n, 6) in WAV order FL FR FC LFE BL BR: the front pair from `front_lr` (mono or (n, 2)), the LFE channel from
    `lfe` through the LFE low-pass (Butterworth, 24 dB/oct at 120 Hz by default), the rest silent."""
    front = np.asarray(front_lr, dtype=np.float64)
    if front.ndim == 1:
        front = np.stack([front, front], axis=1)
    out = np.zeros((len(front), 6))
    out[:, :2] = front
    out[:, 3] = svf.process_fast(svf.design_butter(order, lp_hz, fs, "low"), np.asarray(lfe, dtype=np.float64)[:len(front)])
    return out


LOOKAHEAD_S = {"live": 0.0, "mix": 0.045, "print": 0.055}  # engine lookahead E (SPEC §4.4)


def divide_sub(x_b, fs: float, *, shape_pct: float = 0.0, conf_gate_pct: float = 50.0, quality: str = "mix") -> np.ndarray:
    """DIVIDE Sub -1 through the tracking low-pass (shape 0 = sine, 100 = native), confidence-gated. In Mix and
    Print the gate reads the confidence E ahead (R1), so it is open when the note starts."""
    look = LOOKAHEAD_S[quality]
    res = divide.divide(x_b, fs, live=quality == "live", lookahead_s=look)
    y = divide.smooth_sub(res, fs, 1, shape_pct=shape_pct)
    k = min(int(round(look * fs)), len(res.conf))
    ahead = (lambda a, fill: np.concatenate([a[k:], np.full(k, fill)])) if k else (lambda a, fill: a)
    return divide.apply_confidence_gate(y, ahead(res.conf, 0.0), conf_gate_pct, fs, ahead(res.provisional, False))



def divide_sub_causal(x_b, fs: float, *, shape_pct: float = 0.0, conf_gate_pct: float = 50.0,
                      quality: str = "mix") -> np.ndarray:
    """divide_sub with the gate reading the confidence as the plug-in knows it (divide.conf_as_known): the reference
    for the C++ DIVIDE's gated output (Phase 3 Task 8). Equal to divide_sub in Live."""
    look = LOOKAHEAD_S[quality]
    live = quality == "live"
    res = divide.divide(x_b, fs, live=live, lookahead_s=look)
    y = divide.smooth_sub(res, fs, 1, shape_pct=shape_pct)
    conf, prov = divide.conf_as_known(x_b, fs, live=live, lookahead_s=look)
    k = min(int(round(look * fs)), len(conf))
    ahead = (lambda a, fill: np.concatenate([a[k:], np.full(k, fill)])) if k else (lambda a, fill: a)
    return divide.apply_confidence_gate(y, ahead(conf, 0.0), conf_gate_pct, fs, ahead(prov, False))

def track_sub(x_b, fs: float, *, f_min: float = 30.0, lookahead_s: float = 0.045, shape_pct: float = 0.0,
              conf_gate_pct: float = 50.0) -> np.ndarray:
    """TRACK Sub -1: MPM pitch read `lookahead_s` ahead (log-interpolated; 0 = causal hold), free-running oscillator at
    f0/2, amplitude = the source band's envelope now, gated by the pitch confidence."""
    x_b = np.asarray(x_b, dtype=np.float64)
    n = len(x_b)
    if n == 0:
        return x_b.copy()
    trk = track.mpm(x_b, fs, f_min=f_min)
    f0 = track.f0_per_sample(trk, n, fs, lookahead_s=lookahead_s, interp="log" if lookahead_s > 0.0 else "hold")
    f0 = np.nan_to_num(f0, nan=f_min)
    conf = np.interp(np.arange(n) / fs + lookahead_s, trk.t, trk.conf)
    gate = divide.apply_confidence_gate(np.ones(n), conf, conf_gate_pct, fs)
    amp = harmonic.amplitude_envelope(x_b, fs, f_lo=f_min)
    return amp * gate * track.oscillator(f0 / 2.0, fs, shape_pct)


def engine_sub(x_b, fs: float, engine: str, **kw) -> np.ndarray:
    if engine == "divide":
        return divide_sub(x_b, fs, **kw)
    if engine == "track":
        return track_sub(x_b, fs, **kw)
    if engine == "trigger":
        return trigger.render(x_b, fs, **kw)
    if engine == "harmonic":
        return harmonic.harmonic(x_b, fs, **kw)
    raise ValueError(f"unknown engine {engine!r} (choose from {ENGINES})")
