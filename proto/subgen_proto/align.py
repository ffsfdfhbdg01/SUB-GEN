# SPDX-License-Identifier: AGPL-3.0-or-later
"""Auto-Align (SPEC §8.B): the polarity and phase rotation of the sub that make it add best to the dry.

z = dry + p Re{a e^{j theta}}, a = analytic signal of the sub. (p, theta) and (-p, theta + 180) give the same z, so
the search runs over the 360/step distinct rotations psi = theta + 180 (p < 0) and the result is reported with
|theta| <= 90 and a polarity flip instead of a +-180 degree rotation.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import signal as sps

TIE_DB = 1e-3  # candidates within this of the best count as equal; the one nearest the identity wins


@dataclass(frozen=True)
class AlignResult:
    pol: int
    rot_deg: float
    gain_db: float        # against the untouched sub (p = +1, theta = 0)
    confidence_db: float  # best against the median of all rotations; < 0.5 dB means no clear optimum


def _wrap(a):
    return (a + 180.0) % 360.0 - 180.0


def auto_align(dry, sub, fs: float, band, objective: str = "energy", step_deg: float = 15.0) -> AlignResult:
    """objective 'energy': most energy of z in `band` (Hz); 'headroom': smallest peak |z|."""
    if objective not in ("energy", "headroom"):
        raise ValueError(f"unknown objective {objective!r}")
    dry = np.asarray(dry, dtype=np.float64)
    sub = np.asarray(sub, dtype=np.float64)
    n_psi = int(round(360.0 / step_deg))
    psi = np.arange(n_psi) * (360.0 / n_psi)
    c, s = np.cos(np.radians(psi)), np.sin(np.radians(psi))
    a = sps.hilbert(sub)
    re, im = a.real, a.imag  # Re{a e^{j psi}} = cos(psi) re - sin(psi) im
    if objective == "energy":
        w = np.hanning(len(dry))
        keep = (np.fft.rfftfreq(len(dry), 1.0 / fs) >= band[0]) & (np.fft.rfftfreq(len(dry), 1.0 / fs) <= band[1])
        d_, r_, i_ = (np.fft.rfft(w * v)[keep] for v in (dry, re, im))
        value = np.array([np.sum(np.abs(d_ + c[k] * r_ - s[k] * i_) ** 2) for k in range(n_psi)])
        score = 10.0 * np.log10(np.maximum(value, 1e-300))  # larger is better; silence: all equal (D8 M6)
    else:
        value = np.array([np.max(np.abs(dry + c[k] * re - s[k] * im)) for k in range(n_psi)])
        score = -20.0 * np.log10(np.maximum(value, 1e-300))
    ok = np.flatnonzero(score >= np.max(score) - TIE_DB)
    k = ok[np.argmin(np.abs(_wrap(psi[ok])) + 1e-9 * (_wrap(psi[ok]) < 0))]  # nearest the identity, + before -
    psi_c = float(_wrap(psi[k]))
    pol, theta = (1, psi_c) if -90.0 < psi_c <= 90.0 else (-1, float(_wrap(psi_c - 180.0)))
    return AlignResult(pol, theta, float(score[k] - score[0]), float(score[k] - np.median(score)))
