# SPDX-License-Identifier: AGPL-3.0-or-later
"""HARMONIC engine (SPEC §6.5): virtual pitch by Chebyshev waveshaping of the amplitude-normalised source band.

y_h = e * sum_{k=2..K} w_k T_k(x_n),  x_n = clamp(x / max(e, E_FLOOR), -1, 1),  w_k = (amount/100) c_k / k,
c_k = 1 - b (even k) or 1 + b (odd k) with b = balance/100, clipped at 0. For a sine of amplitude e, T_k(cos t) = cos(k t),
so the k-th harmonic has amplitude e * w_k exactly. A 15 Hz high-pass removes the DC of the even terms.
"""
from __future__ import annotations

import numpy as np

from . import envelope, svf

HP_HZ = 15.0
HP_ORDER = 2


def amplitude_envelope(x, fs: float, *, f_lo: float = 30.0, att_ms: float = 5.0, rel_ms: float = 60.0) -> np.ndarray:
    """Smoothed amplitude e of the source band. A max-hold over 1.5 periods of the lowest band frequency `f_lo`
    makes it ripple-free on bass sines; the attack / release smoother follows. (A plain 5 / 60 ms peak follower
    sits ~1.05 dB below the amplitude of a 30-120 Hz sine and ripples 0.2-0.7 dB.)"""
    hold = envelope.sliding_peak(np.abs(x), max(int(np.ceil(1.5 * fs / f_lo)), 1))
    return envelope.peak_follower(hold, fs, att_ms, rel_ms)


def chebyshev_sum(xn, amount_pct: float, balance: float, k_max: int) -> np.ndarray:
    """sum_{k=2..K} w_k T_k(xn) by the recurrence T_{k+1} = 2 x T_k - T_{k-1}."""
    b = balance / 100.0
    t_prev, t_cur = np.ones_like(xn), xn  # T_0, T_1
    y = np.zeros_like(xn)
    for k in range(2, k_max + 1):
        t_prev, t_cur = t_cur, 2.0 * xn * t_cur - t_prev  # T_k
        y += (amount_pct / 100.0) * max((1.0 - b) if k % 2 == 0 else (1.0 + b), 0.0) / k * t_cur
    return y


def harmonic(x_b, fs: float, *, amount_pct: float = 50.0, balance: float = 0.0, k_max: int = 4, att_ms: float = 5.0,
             rel_ms: float = 60.0, e_floor_db: float = -80.0, f_lo: float = 30.0) -> np.ndarray:
    """Harmonics 2..k_max of the source band, scaled by its envelope (the fundamental is not included)."""
    x = np.asarray(x_b, dtype=np.float64)
    if x.size == 0:
        return x.copy()
    e = amplitude_envelope(x, fs, f_lo=f_lo, att_ms=att_ms, rel_ms=rel_ms)
    xn = np.clip(x / np.maximum(e, 10.0 ** (e_floor_db / 20.0)), -1.0, 1.0)
    return svf.process_fast(svf.design_butter(HP_ORDER, HP_HZ, fs, "high"), e * chebyshev_sum(xn, amount_pct, balance, k_max))
