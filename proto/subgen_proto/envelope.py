# SPDX-License-Identifier: AGPL-3.0-or-later
"""Envelope followers."""
from __future__ import annotations

import numpy as np
from scipy import ndimage, signal as sps


def peak_follower(x, fs: float, attack_ms: float, release_ms: float) -> np.ndarray:
    """Rectified one-pole follower with separate attack and release time constants (ms)."""
    a_att = 1.0 - np.exp(-1.0 / (attack_ms * 1e-3 * fs))
    a_rel = 1.0 - np.exp(-1.0 / (release_ms * 1e-3 * fs))
    x = np.abs(np.asarray(x, dtype=np.float64))
    out = [0.0] * len(x)
    y = 0.0
    for i, v in enumerate(x.tolist()):
        y += (a_att if v > y else a_rel) * (v - y)
        out[i] = y
    return np.array(out)


def rms_follower(x, fs: float, tau_ms: float) -> np.ndarray:
    """sqrt of a one-pole average of x^2 (time constant tau_ms)."""
    a = 1.0 - np.exp(-1.0 / (tau_ms * 1e-3 * fs))
    x = np.asarray(x, dtype=np.float64)
    return np.sqrt(sps.lfilter([a], [1.0, -(1.0 - a)], x * x))


def sliding_peak(x, window: int) -> np.ndarray:
    """Causal running maximum: y[n] = max(x[n - window + 1 .. n]) (zero-padded before the start)."""
    return ndimage.maximum_filter1d(np.asarray(x, dtype=np.float64), size=window, origin=(window - 1) // 2)
