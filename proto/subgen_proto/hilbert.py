# SPDX-License-Identifier: AGPL-3.0-or-later
"""All-pass Hilbert pair (SPEC §7.7): two chains of first-order all-passes whose outputs differ by 90 degrees.

Section (p - s)/(p + s) has phase -2 atan(w/p). Chain A takes the lower poles, chain B the interleaved upper ones, so
B leads A by 90 degrees inside the band; the real poles come from a minimax fit (least squares, then an epigraph
SLSQP) started from a geometric interleave. Digital: bilinear without pre-warping, H(z) = (a + z^-1)/(1 + a z^-1),
a = (g - 1)/(g + 1), g = p/(2 fs): the warping is below 0.02 % of the frequency for 600 Hz at 44.1 kHz.
"""
from __future__ import annotations

import functools
from dataclasses import dataclass

import numpy as np
from scipy import optimize, signal as sps


@dataclass(frozen=True)
class HilbertDesign:
    poles_a: np.ndarray  # rad/s
    poles_b: np.ndarray  # rad/s
    max_err_deg: float   # max |phase(B) - phase(A) - 90| over the design band


def phase_difference_deg(design: HilbertDesign, f_hz) -> np.ndarray:
    """phase(B) - phase(A) of the analogue prototype in degrees (90 = ideal)."""
    w = 2.0 * np.pi * np.asarray(f_hz, dtype=float)[:, None]
    return np.degrees(2.0 * (np.sum(np.arctan(w / design.poles_a), axis=1) - np.sum(np.arctan(w / design.poles_b), axis=1)))


def _error(logp, n, w):
    pa, pb = np.exp(logp[:n]), np.exp(logp[n:])
    return np.degrees(2.0 * (np.sum(np.arctan(w[:, None] / pa), axis=1) - np.sum(np.arctan(w[:, None] / pb), axis=1))) - 90.0


@functools.lru_cache(maxsize=None)
def _design(n: int, f_lo: float, f_hi: float):
    w = 2.0 * np.pi * np.geomspace(f_lo, f_hi, 240)
    lo, hi = np.log(2.0 * np.pi * f_lo), np.log(2.0 * np.pi * f_hi)
    x0 = np.linspace(lo - 0.8, hi + 0.8, 2 * n)  # geometric interleave, a little wider than the band
    x0 = np.concatenate([x0[0::2], x0[1::2]])    # chain A: the 1st, 3rd, ... pole; chain B: the 2nd, 4th, ...
    x1 = optimize.least_squares(lambda v: _error(v, n, w), x0).x
    t0 = float(np.max(np.abs(_error(x1, n, w))))
    cons = [{"type": "ineq", "fun": lambda v: v[-1] - _error(v[:-1], n, w)},
            {"type": "ineq", "fun": lambda v: v[-1] + _error(v[:-1], n, w)}]
    res = optimize.minimize(lambda v: v[-1], np.concatenate([x1, [t0]]), constraints=cons, method="SLSQP",
                            options={"maxiter": 400, "ftol": 1e-10})
    dense = 2.0 * np.pi * np.geomspace(f_lo, f_hi, 4000)
    best = min((x1, res.x[:-1]), key=lambda v: np.max(np.abs(_error(v, n, dense))))
    p = np.exp(best)
    return np.sort(p[:n]), np.sort(p[n:]), float(np.max(np.abs(_error(best, n, dense))))


def design(n_sections: int, f_lo: float = 12.0, f_hi: float = 600.0) -> HilbertDesign:
    """Minimax design with n_sections first-order all-passes per chain."""
    pa, pb, err = _design(int(n_sections), float(f_lo), float(f_hi))
    return HilbertDesign(pa.copy(), pb.copy(), err)


def min_sections(error_deg: float = 1.0, f_lo: float = 12.0, f_hi: float = 600.0, n_max: int = 12) -> int:
    """Smallest section count per chain whose design reaches `error_deg`."""
    for n in range(1, n_max + 1):
        if design(n, f_lo, f_hi).max_err_deg <= error_deg:
            return n
    raise ValueError(f"no design up to {n_max} sections reaches {error_deg} degrees")


def allpass_chain(x, poles_rad_s, fs: float) -> np.ndarray:
    y = np.asarray(x, dtype=np.float64)
    for p in poles_rad_s:
        g = p / (2.0 * fs)
        a = (g - 1.0) / (g + 1.0)
        y = sps.lfilter([a, 1.0], [1.0, a], y)
    return y


def rotate(x, theta_deg: float, d: HilbertDesign, fs: float) -> np.ndarray:
    """y = cos(theta) A + sin(theta) B: B leads A by 90 degrees, so positive theta leads in phase. theta = 0 is chain A."""
    th = np.radians(theta_deg)
    return np.cos(th) * allpass_chain(x, d.poles_a, fs) + np.sin(th) * allpass_chain(x, d.poles_b, fs)


def polarity(x, pol: int) -> np.ndarray:
    """Polarity switch: an exact multiplication by +1 or -1."""
    return np.asarray(x, dtype=np.float64) * float(pol)
