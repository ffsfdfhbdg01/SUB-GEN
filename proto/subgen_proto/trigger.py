# SPDX-License-Identifier: AGPL-3.0-or-later
"""TRIGGER engine (SPEC §6.4): a synthetic sub started by a transient.

detect_onsets  fast envelope of |x| (attack 0.2 ms, release 15 ms) crossing a threshold, with 3 dB hysteresis, a
               re-arm hold and a lockout; the crossing time is interpolated linearly (a fractional sample index)
voice          closed-form pitch-dropping sine with an attack ramp and a T60 decay, started at a fractional time
render         onsets -> voices (velocity, optional Follow), Restart (3 ms crossfade) or Legato (continuous phase)
"""
from __future__ import annotations

import math

import numpy as np

from . import envelope

HYST_DB = 3.0
ATTACK_MS, RELEASE_MS = 0.2, 15.0  # fast envelope
VEL_REF, VEL_MAX = 0.5, 1.5        # V = clamp((peak / 0.5)^(vel/100), 0, 1.5)
PEAK_WINDOW_MS = 20.0              # peak of x_b after the onset that sets the velocity
CROSSFADE_MS = 3.0                 # Restart: old voice fades out over this time
RMS_TAU_MS = 20.0                  # Follow: e_rms (SPEC §5.4)
DEFAULTS = dict(f_start=80.0, f_end=45.0, tau_p_ms=30.0, attack_ms=2.0, t60_ms=350.0, phase0_deg=0.0)


def detect_onsets(x, fs, *, thresh_db: float = -30.0, hold_ms: float = 20.0, lockout_ms: float = 60.0):
    """Fractional sample indices of the upward crossings of `thresh_db` (dBFS, peak) by the fast envelope. A
    crossing counts if the detector is armed (the envelope was below thresh - 3 dB for `hold_ms`, or nothing has
    fired yet) and at least `lockout_ms` have passed since the last event."""
    e = envelope.peak_follower(x, fs, ATTACK_MS, RELEASE_MS)
    hi = 10.0 ** (thresh_db / 20.0)
    lo = hi * 10.0 ** (-HYST_DB / 20.0)
    up = np.flatnonzero((e[:-1] < hi) & (e[1:] >= hi)) + 1
    hold = int(round(hold_ms * 1e-3 * fs))
    edges = np.diff(np.concatenate([[0], (e < lo).astype(np.int8), [0]]))
    starts, ends = np.flatnonzero(edges == 1), np.flatnonzero(edges == -1)
    rearm = (starts + hold)[(ends - starts) >= max(hold, 1)]  # instants at which a long enough dip is complete
    lockout = lockout_ms * 1e-3 * fs
    out: list[float] = []
    last_i, last_t = -1, -math.inf
    for i in up:
        k = int(np.searchsorted(rearm, last_i, side="right"))
        t = (i - 1) + (hi - e[i - 1]) / (e[i] - e[i - 1])
        if (not out or (k < len(rearm) and rearm[k] <= i)) and t - last_t >= lockout:
            out.append(float(t))
            last_i, last_t = int(i), t
    return np.array(out)


def _phase_inc(s, f_start, f_end, tau):
    """2 pi * integral of f from 0 to s, f(t) = f_end + (f_start - f_end) exp(-t/tau)."""
    return 2.0 * np.pi * (f_end * s + (f_start - f_end) * tau * (1.0 - np.exp(-s / tau)))


def _envelope(s, t_a, t60, a0, a1):
    """Attack from a0 to a1 over t_a (linear), then a1 * 10^(-3 (s - t_a) / T60); s >= 0."""
    ramp = a0 + (a1 - a0) * (s / t_a if t_a > 0.0 else 1.0)
    return np.where(s < t_a, ramp, a1 * 10.0 ** (-3.0 * (s - t_a) / t60))


def _segment(s, prm, phi1, phi2, a0, v):
    """Both subs at voice times s >= 0, starting phases phi1 / phi2 and amplitude a0 -> v."""
    inc = _phase_inc(s, prm["f_start"], prm["f_end"], prm["tau_p_ms"] * 1e-3)
    env = _envelope(s, prm["attack_ms"] * 1e-3, prm["t60_ms"] * 1e-3, a0, v)
    return env * np.sin(phi1 + inc), env * np.sin(phi2 + 0.5 * inc)


def voice(fs, n, *, f_start=80.0, f_end=45.0, tau_p_ms=30.0, attack_ms=2.0, t60_ms=350.0, phase0_deg=0.0,
          t_on_frac=0.0):
    """(Sub -1, Sub -2) of length n. The onset (voice time 0) is `t_on_frac` samples after sample 0 (negative: the
    voice is already running at sample 0); earlier samples are 0. Sub -2 has the same envelope and half the
    frequency, starting at the same phase."""
    prm = dict(f_start=f_start, f_end=f_end, tau_p_ms=tau_p_ms, attack_ms=attack_ms, t60_ms=t60_ms)
    t = (np.arange(n) - t_on_frac) / fs
    on = t >= 0.0
    phi0 = math.radians(phase0_deg)
    y1, y2 = _segment(np.where(on, t, 0.0), prm, phi0, phi0, 0.0, 1.0)
    return np.where(on, y1, 0.0), np.where(on, y2, 0.0)


def _velocity(x, fs, t_on, velocity_pct):
    if velocity_pct <= 0.0:
        return 1.0
    k0 = int(math.ceil(t_on))
    peak = float(np.max(np.abs(x[k0:k0 + int(round(PEAK_WINDOW_MS * 1e-3 * fs))]), initial=0.0))
    return float(np.clip((peak / VEL_REF) ** (velocity_pct / 100.0), 0.0, VEL_MAX))


def _follow_gain(e, a, b, end):
    """Gain over samples [a, end): 1 until the maximum of the smoothed source level e inside the hit [a, b), then
    e / maximum, never rising again (the sub ends with the source)."""
    g = np.ones(end - a)
    seg = e[a:b]
    if seg.size and seg.max() > 0.0:
        k = int(np.argmax(seg))
        g[k:] = np.minimum.accumulate(np.minimum(e[a + k:end] / seg[k], 1.0))
    return g


def render(x, fs, *, retrigger: str = "restart", velocity_pct: float = 60.0, follow: bool = False,
           thresh_db: float = -30.0, hold_ms: float = 20.0, lockout_ms: float = 60.0, sub1_level: float = 1.0,
           sub2_level: float = 0.0, **voice_kwargs):
    """Detect onsets in x (the band-limited source x_b) and render sub1_level * Sub -1 + sub2_level * Sub -2,
    aligned to the fractional onset times. Restart: every onset starts a new voice from phase0 and the old one
    fades out over 3 ms. Legato: the phase runs on, and amplitude and pitch start over from the current
    amplitude. Follow multiplies each voice by the smoothed source level, normalised to its maximum in the hit."""
    unknown = set(voice_kwargs) - set(DEFAULTS)
    if unknown or retrigger not in ("restart", "legato"):
        raise TypeError(f"unknown voice parameters {sorted(unknown)} or retrigger {retrigger!r}")
    prm = {**DEFAULTS, **voice_kwargs}
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    onsets = detect_onsets(x, fs, thresh_db=thresh_db, hold_ms=hold_ms, lockout_ms=lockout_ms)
    out1, out2 = np.zeros(n), np.zeros(n)
    e = envelope.rms_follower(x, fs, RMS_TAU_MS) if follow else None
    fade = max(int(round(CROSSFADE_MS * 1e-3 * fs)), 1)
    phi1 = phi2 = math.radians(prm["phase0_deg"])
    a_cur = 0.0
    for i, t_on in enumerate(onsets):
        last = i + 1 == len(onsets)
        t_next = None if last else float(onsets[i + 1])
        v = _velocity(x, fs, t_on, velocity_pct)
        a = int(math.floor(t_on))
        b = n if last else int(math.floor(t_next))
        if retrigger == "restart":
            end = n if last else min(n, b + fade + 1)
            y1, y2 = voice(fs, end - a, t_on_frac=t_on - a, **prm)
            w = 1.0 if last else np.cos(0.5 * np.pi * np.clip((np.arange(a, end) - t_next) / fade, 0.0, 1.0)) ** 2
            g = _follow_gain(e, a, b, end) if follow else 1.0
            out1[a:end] += v * g * w * y1
            out2[a:end] += v * g * w * y2
        else:
            k0, k1 = int(math.ceil(t_on)), (n if last else int(math.ceil(t_next)))
            y1, y2 = _segment((np.arange(k0, k1) - t_on) / fs, prm, phi1, phi2, a_cur, v)
            g = _follow_gain(e, k0, max(b, k0), k1) if follow else np.ones(k1 - k0)
            out1[k0:k1] += g * y1
            out2[k0:k1] += g * y2
            if not last:  # carry phase and amplitude of the running voice to the next onset
                s_end = (t_next - t_on) / fs
                inc = _phase_inc(s_end, prm["f_start"], prm["f_end"], prm["tau_p_ms"] * 1e-3)
                phi1, phi2 = phi1 + inc, phi2 + 0.5 * inc
                a_cur = float(_envelope(s_end, prm["attack_ms"] * 1e-3, prm["t60_ms"] * 1e-3, a_cur, v))
                if follow and len(g):
                    a_cur *= float(g[min(max(b - k0, 0), len(g) - 1)])
    return sub1_level * out1 + sub2_level * out2
