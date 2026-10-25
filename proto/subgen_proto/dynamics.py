# SPDX-License-Identifier: AGPL-3.0-or-later
"""Sub-path dynamics (SPEC §7.4 compressor, §7.5 gate, §7.8 limiter).

Detectors work over w = max(10 ms, 1.5 T_sub), T_sub = 1/f_sub1 (the Sub -1 frequency, known from the engine):
  'rms'          mean square over w (3 periods of x^2 at f_sub1) followed by a hold over w: a steady tone of any
                 frequency above 1/w reads a constant level, so the gain does not ripple (recommended)
  'peak'         hold of |x| over w
  'rms_window'   SPEC-literal mean square over w without the hold: exact only at f_sub1 and its harmonics
  'rms_onepole'  one-pole mean square (time constant w), 'peak_instant' |x|: ripple-prone, kept for the P2 comparison
The gain reduction is computed in dB from the static curve and smoothed by a one-pole with separate attack and release.
Peak is the default detector (R4, D8 I3): at a note onset it splatters least (-56 dB above 100 Hz at a 1 ms attack, RMS
-39 dB); 'rms' keeps an attack floor of 0.25 T_sub (-42 dB). The SPEC 0.1 ripple rule (attack >= 0.25 T_sub, release
>= 1.0 T_sub for every detector) is kept only for the P2 comparison (ripple_rule=True).
"""
from __future__ import annotations

import numpy as np
from scipy import signal as sps

from . import drive, envelope

AUTO_FAST_MS, AUTO_SLOW_MS, AUTO_SLOW_ATTACK_MS = 50.0, 500.0, 150.0
DETECTORS = ("rms", "peak", "rms_window", "rms_onepole", "peak_instant")


def _window(fs: float, f_sub1: float) -> int:
    return max(1, int(round(max(0.010, 1.5 / f_sub1) * fs)))


def _moving_mean(v, w: int) -> np.ndarray:
    c = np.concatenate([[0.0], np.cumsum(v)])
    n = np.arange(1, len(v) + 1)
    return (c[n] - c[np.maximum(n - w, 0)]) / w


def detector_db(x, fs: float, detect: str = "rms", f_sub1: float = 40.0) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    w = _window(fs, f_sub1)
    if detect == "rms":
        return 10.0 * np.log10(np.maximum(envelope.sliding_peak(_moving_mean(x * x, w), w), 1e-30))
    if detect == "rms_window":
        return 10.0 * np.log10(np.maximum(_moving_mean(x * x, w), 1e-30))
    if detect == "rms_onepole":
        a = 1.0 - np.exp(-1.0 / w)
        return 10.0 * np.log10(np.maximum(sps.lfilter([a], [1.0, -(1.0 - a)], x * x), 1e-30))
    if detect == "peak":
        return 20.0 * np.log10(np.maximum(envelope.sliding_peak(np.abs(x), w), 1e-15))
    if detect == "peak_instant":
        return 20.0 * np.log10(np.maximum(np.abs(x), 1e-15))
    raise ValueError(f"unknown detector {detect!r}")


def static_gr_db(level_db, thresh_db: float, ratio: float, knee_db: float) -> np.ndarray:
    """Gain reduction (dB, <= 0) of the SPEC §7.4 static curve."""
    lv = np.asarray(level_db, dtype=np.float64)
    d = lv - thresh_db
    out = np.where(d > 0.0, thresh_db + d / ratio, lv) if knee_db <= 0.0 else np.where(
        2.0 * d < -knee_db, lv, np.where(2.0 * d > knee_db, thresh_db + d / ratio,
                                         lv + (1.0 / ratio - 1.0) * (d + knee_db / 2.0) ** 2 / (2.0 * knee_db)))
    return out - lv


def effective_times(attack_ms: float, release_ms: float, f_sub1: float, ripple_rule: bool = True):
    """Ripple rule: attack >= 0.25 T_sub, release >= 1.0 T_sub (ms)."""
    if not ripple_rule:
        return attack_ms, release_ms
    t_ms = 1000.0 / f_sub1
    return max(attack_ms, 0.25 * t_ms), max(release_ms, 1.0 * t_ms)


def smooth_gr(target_db, fs: float, attack_ms: float, release_ms: float, start: float = 0.0) -> np.ndarray:
    """One-pole in dB: attack while the reduction deepens, release while it recovers."""
    a_att = 1.0 - np.exp(-1.0 / (attack_ms * 1e-3 * fs))
    a_rel = 1.0 - np.exp(-1.0 / (release_ms * 1e-3 * fs))
    out = np.empty(len(target_db))
    g = start
    for i, t in enumerate(np.asarray(target_db, dtype=np.float64).tolist()):
        g += (a_att if t < g else a_rel) * (t - g)
        out[i] = g
    return out


def compressor(x, fs: float, *, thresh_db: float, ratio: float, knee_db: float, attack_ms: float, release_ms: float,
               detect: str = "peak", f_sub1: float = 40.0, ripple_rule: bool = True, auto_release: bool = False,
               makeup_db: float = 0.0):
    """Returns (y, gr_db). auto_release mixes a 50 ms and a 500 ms release: the slow branch (attack 150 ms) only
    builds up under sustained reduction, and the deeper of the two branches wins."""
    x = np.asarray(x, dtype=np.float64)
    target = static_gr_db(detector_db(x, fs, detect, f_sub1), thresh_db, ratio, knee_db)
    att, rel = effective_times(attack_ms, release_ms, f_sub1, ripple_rule)
    if detect == "rms":
        att = max(att, 250.0 / f_sub1)  # R4: the RMS detector keeps an attack floor of a quarter sub period
    if auto_release:
        fast = smooth_gr(target, fs, att, effective_times(attack_ms, AUTO_FAST_MS, f_sub1, ripple_rule)[1])
        slow = smooth_gr(target, fs, max(att, AUTO_SLOW_ATTACK_MS), AUTO_SLOW_MS)
        gr = np.minimum(fast, slow)
    else:
        gr = smooth_gr(target, fs, att, rel)
    return x * 10.0 ** ((gr + makeup_db) / 20.0), gr


def gate_gain_db(open_mask, fs: float, *, range_db: float, attack_ms: float, release_ms: float, f_sub1: float = 40.0):
    """Gate gain (dB): 0 while open, range_db while closed, smoothed (attack on opening, release on closing), both
    no faster than 0.25 T_sub."""
    floor = 250.0 / f_sub1
    target = np.where(np.asarray(open_mask, dtype=bool), 0.0, range_db)
    a_open = 1.0 - np.exp(-1.0 / (max(attack_ms, floor) * 1e-3 * fs))
    a_close = 1.0 - np.exp(-1.0 / (max(release_ms, floor) * 1e-3 * fs))
    out = np.empty(len(target))
    g = float(target[0]) if len(target) else 0.0
    for i, t in enumerate(target.tolist()):
        g += (a_open if t > g else a_close) * (t - g)
        out[i] = g
    return out


def gate(x, fs: float, *, thresh_db: float, range_db: float, hold_ms: float, release_ms: float, attack_ms: float = 2.0,
         hyst_db: float = 3.0, f_sub1: float = 40.0):
    """Downward expander with hysteresis: opens when the peak-hold envelope reaches thresh_db, closes after it has
    stayed below thresh_db - hyst_db for longer than hold_ms. Returns (y, open_mask)."""
    x = np.asarray(x, dtype=np.float64)
    env = detector_db(x, fs, "peak", f_sub1)
    hold = int(round(hold_ms * 1e-3 * fs))
    lo = thresh_db - hyst_db
    mask = np.empty(len(x), dtype=bool)
    is_open, below = False, 0
    for i, e in enumerate(env.tolist()):
        if not is_open:
            if e >= thresh_db:
                is_open, below = True, 0
        elif e < lo:
            below += 1
            if below > hold:
                is_open = False
        else:
            below = 0
        mask[i] = is_open
    gain = gate_gain_db(mask, fs, range_db=range_db, attack_ms=attack_ms, release_ms=release_ms, f_sub1=f_sub1)
    return x * 10.0 ** (gain / 20.0), mask


def limiter(x, fs: float, *, ceiling_db: float, lookahead_ms: float, release_ms: float, live: bool = False,
            return_gain: bool = False):
    """Live: c tanh(x/c) with 1st-order ADAA (|y| < c). Otherwise: required gain r = min(1, c/|x|), held (min) over
    the D + 1 = round(lookahead fs) + 1 most recent samples, released with a one-pole, smoothed by a positive Hann
    FIR of length D + 1 and applied to x delayed by exactly D samples. Every tap of the smoothing sees a held value
    that already covers the delayed sample, so |y| <= c. return_gain=True returns (y, gain) in the lookahead modes
    (the C++ golden data)."""
    x = np.asarray(x, dtype=np.float64)
    c = 10.0 ** (ceiling_db / 20.0)
    if live:
        return drive._adaa(x, lambda u: c * np.tanh(u / c), lambda u: c * c * drive._log_cosh(u / c))
    d = int(round(lookahead_ms * 1e-3 * fs))
    if x.size == 0:
        return x.copy()
    ax = np.abs(x)
    r = np.where(ax > c, c / np.maximum(ax, 1e-300), 1.0)
    h = -envelope.sliding_peak(-r, d + 1, initial=-1.0) if d > 0 else r  # causal min; gain 1 before the start
    a_rel = 1.0 - np.exp(-1.0 / (release_ms * 1e-3 * fs))
    e = np.empty_like(h)
    g = 1.0
    for i, v in enumerate(h.tolist()):
        g = v if v < g else g + a_rel * (v - g)
        e[i] = g
    w = np.hanning(d + 3)[1:-1]
    gain = sps.lfilter(w / np.sum(w), [1.0], e) if d > 0 else e
    delayed = np.concatenate([np.zeros(min(d, len(x))), x[:max(len(x) - d, 0)]]) if d > 0 else x
    return (delayed * gain, gain) if return_gain else delayed * gain
