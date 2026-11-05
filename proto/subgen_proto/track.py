# SPDX-License-Identifier: AGPL-3.0-or-later
"""TRACK engine pieces (SPEC §6.3): pitch detectors (NSDF/MPM, YIN, PLL), the continuity prior, per-sample f0 for
the oscillator, and the sub oscillator.

The detectors work on x_b decimated to ~2 kHz. A frame stamped t holds the last W decimated samples up to t (zeros
before the start of the signal), so t is the moment its estimate becomes available and the estimate describes the
pitch around t - W/2. Frames are *onset aware*: leading samples below 1 % of the frame peak are dropped and only
lags up to half of what is left are searched, so a note is measurable after ~2 periods and its confidence is valid
at once instead of rising slowly as the note fills the window.
"""
from __future__ import annotations

import functools
import math
from dataclasses import dataclass

import numpy as np
from scipy import signal as sps

CLARITY_FLOOR = 0.6  # clarity at or below this maps to confidence 0 (band-limited noise sits at 0.45-0.56)
TRIM_REL = 0.01      # leading samples below this fraction of the frame peak count as silence
MPM_K = 0.9          # key-maximum threshold (SPEC §6.3)
YIN_THRESHOLD = 0.15


@dataclass
class Track:
    t: np.ndarray          # s: moment the estimate becomes available (frame end)
    f0: np.ndarray         # Hz, NaN until the first estimate
    conf: np.ndarray       # 0..1: calibrated clarity
    fs_d: float = 0.0      # effective decimated rate (Hz)
    window_s: float = 0.0  # analysis window W / fs_d (s)


# ------------------------------------------------------------------------------------------------ front end
def decimation_plan(fs: float, fs_d_target: float = 2000.0) -> tuple[int, float]:
    """Integer decimation factor D = floor(fs / target) and the effective rate fs / D (>= target)."""
    d = max(1, int(np.floor(fs / fs_d_target + 1e-9)))
    return d, fs / d


@functools.lru_cache(maxsize=None)
def _aa_sos(fs: float):
    return sps.butter(8, 800.0, fs=fs, output="sos")


def _decimate(x, fs: float, fs_d_target: float):
    """Causal anti-alias filter (8th-order Butterworth at 800 Hz, SPEC §6.3) and every D-th sample (D8 M13: the
    zero-phase resample_poly used before rang ahead of the signal)."""
    d, fs_d = decimation_plan(fs, fs_d_target)
    x = np.asarray(x, dtype=np.float64)
    if d == 1 or len(x) == 0:
        return x, d, fs_d
    return sps.sosfilt(_aa_sos(float(fs)), x)[::d], d, fs_d


def _trim(frame) -> int:
    """Index of the first sample above TRIM_REL * peak; len(frame) for a silent frame."""
    a = np.abs(frame)
    pk = a.max()
    return len(frame) if pk == 0.0 else int(np.argmax(a > TRIM_REL * pk))


# ------------------------------------------------------------------------------------------------ detectors
def _nsdf(g):
    """n'(tau) = 2 r'(tau) / m'(tau), r' = sum g_j g_{j+tau}, m' = sum (g_j^2 + g_{j+tau}^2) over the overlap."""
    n = len(g)
    r = np.correlate(g, g, mode="full")[n - 1:]
    c = np.concatenate([[0.0], np.cumsum(g * g)])
    tau = np.arange(n)
    m = c[n - tau] + (c[n] - c[tau])
    return np.where(m > 0.0, 2.0 * r / np.where(m > 0.0, m, 1.0), 0.0)


def _parabola(a: float, b: float, c: float):
    """Offset (in samples) and value of the extremum of the parabola through (-1, a), (0, b), (1, c)."""
    den = a - 2.0 * b + c
    delta = 0.5 * (a - c) / den if den != 0.0 else 0.0
    return delta, b - 0.25 * (a - c) * delta


def _mpm_pick(g, tau_lo: int, tau_hi: int):
    """First key maximum within [tau_lo, tau_hi] reaching MPM_K x the highest one: (lag, height) or (nan, 0)."""
    s = _nsdf(g)
    n = len(s)
    d = np.diff((s >= 0.0).astype(np.int8))
    ups = np.flatnonzero(d == 1) + 1      # first sample of a positive lobe
    downs = np.flatnonzero(d == -1) + 1   # first sample after a positive lobe
    if downs.size == 0:
        return math.nan, 0.0
    peaks = []
    for u in ups[ups > downs[0]]:         # the lobe around lag 0 is skipped
        if u > tau_hi:
            break
        after = downs[downs > u]
        p = int(u) + int(np.argmax(s[u:(after[0] if after.size else n)]))
        if tau_lo <= p <= tau_hi and 1 <= p <= n - 2 and s[p] >= s[p - 1] and s[p] >= s[p + 1]:
            peaks.append((p, s[p]))
    if not peaks:
        return math.nan, 0.0
    top = max(h for _, h in peaks)
    p = next(p for p, h in peaks if h >= MPM_K * top)
    delta, h = _parabola(s[p - 1], s[p], s[p + 1])
    return p + delta, h


def _yin_pick(g, tau_lo: int, tau_hi: int):
    """YIN: first lag in [tau_lo, tau_hi] whose cumulative-mean-normalised difference is below the threshold
    (then its local minimum), else the global minimum: (lag, clarity = 1 - d') or (nan, 0)."""
    n = len(g)
    tmax = tau_hi + 1                       # one extra lag for the parabola
    w = n - tmax                            # fixed integration window
    if w < 2:
        return math.nan, 0.0
    c = np.concatenate([[0.0], np.cumsum(g * g)])
    tau = np.arange(tmax + 1)
    cross = np.correlate(g, g[:w], mode="valid")                  # sum_{j<w} g_j g_{j+tau}
    d = np.maximum(c[w] + (c[tau + w] - c[tau]) - 2.0 * cross, 0.0)
    cum = np.cumsum(d[1:])
    if cum[-1] <= 0.0:
        return math.nan, 0.0
    dp = np.ones(tmax + 1)
    dp[1:] = d[1:] * tau[1:] / np.where(cum > 0.0, cum, 1.0)
    seg = dp[tau_lo:tau_hi + 1]
    below = np.flatnonzero(seg < YIN_THRESHOLD)
    if below.size:
        p = tau_lo + int(below[0])
        while p + 1 <= tau_hi and dp[p + 1] < dp[p]:
            p += 1
    else:
        p = tau_lo + int(np.argmin(seg))
    if dp[p] > dp[p - 1] or dp[p] > dp[p + 1]:  # the minimum lies outside the searched lags: no estimate yet
        return math.nan, 0.0
    delta, v = _parabola(dp[p - 1], dp[p], dp[p + 1])
    return p + delta, 1.0 - v


def apply_prior(f_raw, conf, *, window: float = 0.25, confirm: int = 3, voiced: float = 0.5, lost_frames: int = 5):
    """Continuity prior (SPEC §6.3). A raw value outside +-`window` of the accepted one is held back and taken
    only after `confirm` consecutive frames that agree with each other (octave jumps included); the same rule
    seeds the first value, so a lone outlier cannot start the track wrong. Frames below `voiced` confidence are
    ignored (the last value is held; NaN before the first one) and `lost_frames` of them in a row forget the track."""
    f_raw = np.asarray(f_raw, dtype=np.float64)
    conf = np.asarray(conf, dtype=np.float64)
    out = np.empty_like(f_raw)
    cur = None
    last = math.nan
    pend, pend_val, quiet = 0, math.nan, 0
    for i in range(len(f_raw)):
        f = f_raw[i]
        if not (conf[i] >= voiced and math.isfinite(f)):
            quiet += 1
            if quiet >= lost_frames:
                cur, pend = None, 0
            out[i] = last
            continue
        quiet = 0
        if cur is not None and abs(f / cur - 1.0) <= window:
            cur, pend = f, 0
        else:  # no track yet, or outside the window
            pend = pend + 1 if pend and abs(f / pend_val - 1.0) <= window else 1
            pend_val = f
            if pend >= confirm:
                cur, pend = f, 0
        if cur is not None:
            last = cur
        out[i] = last
    return out


def _detect(x, fs, picker, *, f_min, f_max, hop_ms, window_periods, fs_d_target, prior) -> Track:
    y, d, fs_d = _decimate(x, fs, fs_d_target)
    w = int(np.ceil(window_periods * fs_d / max(f_min, 20.0)))
    if len(y) == 0:
        z = np.zeros(0)
        return Track(z, z.copy(), z.copy(), fs_d, w / fs_d)
    tau_max = int(np.ceil(fs_d / f_min))
    tau_min = max(2, int(np.floor(fs_d / f_max)))
    hop = max(1, int(round(hop_ms * 1e-3 * fs_d)))
    frames = np.lib.stride_tricks.sliding_window_view(np.concatenate([np.zeros(w - 1), y]), w)[::hop]
    f_raw = np.full(len(frames), math.nan)
    clarity = np.zeros(len(frames))
    for k, frame in enumerate(frames):
        g = frame[_trim(frame):]
        hi = min(tau_max, len(g) // 2)
        if hi < tau_min + 2:
            continue
        lag, h = picker(g, tau_min, hi)
        if math.isfinite(lag):
            f_raw[k] = fs_d / lag
            clarity[k] = min(max(h, 0.0), 1.0)
    conf = np.clip((clarity - CLARITY_FLOOR) / (1.0 - CLARITY_FLOOR), 0.0, 1.0)
    f0 = apply_prior(f_raw, conf) if prior else f_raw
    return Track(np.arange(len(frames)) * hop * d / fs, f0, conf, fs_d, w / fs_d)


def mpm(x, fs, *, f_min=20.0, f_max=300.0, hop_ms=2.0, window_periods=2.5, fs_d_target=2000.0, prior=True) -> Track:
    """McLeod-Wyvill pitch method on the decimated, onset-trimmed frame; confidence = calibrated NSDF clarity."""
    return _detect(x, fs, _mpm_pick, f_min=f_min, f_max=f_max, hop_ms=hop_ms, window_periods=window_periods,
                   fs_d_target=fs_d_target, prior=prior)


def yin(x, fs, *, f_min=20.0, f_max=300.0, hop_ms=2.0, window_periods=2.5, fs_d_target=2000.0, prior=True) -> Track:
    """YIN on the same frames; confidence = calibrated 1 - d'."""
    return _detect(x, fs, _yin_pick, f_min=f_min, f_max=f_max, hop_ms=hop_ms, window_periods=window_periods,
                   fs_d_target=fs_d_target, prior=prior)


def pll(x, fs, *, f_init: float = 60.0, bw_hz: float = 5.0, zeta: float = 1.0 / math.sqrt(2.0)):
    """Second-order PLL (PI loop filter) on x ~ A cos(phi); returns per-sample (f_hz, phase_rad), the phase
    being that of the analytic signal. The phase detector is -x sin(phi_hat) / (A/2) (A from a 50 ms follower), so
    the loop gain does not depend on level; bw_hz is the closed-loop -3 dB bandwidth."""
    x = np.asarray(x, dtype=np.float64)
    wn = 2.0 * math.pi * bw_hz / math.sqrt(1.0 + 2.0 * zeta ** 2 + math.sqrt((1.0 + 2.0 * zeta ** 2) ** 2 + 1.0))
    kp, ki = 2.0 * zeta * wn, wn * wn
    a = 1.0 - math.exp(-1.0 / (0.05 * fs))
    w, phi, a2 = 2.0 * math.pi * f_init, 0.0, 0.0
    f_out = np.empty(len(x))
    p_out = np.empty(len(x))
    for i, xi in enumerate(x.tolist()):
        f_out[i] = w / (2.0 * math.pi)
        p_out[i] = phi
        a2 += a * (2.0 * xi * xi - a2)
        eps = 0.0
        if a2 > 1e-18:
            eps = max(-2.0, min(2.0, -xi * math.sin(phi) / (0.5 * math.sqrt(a2))))
        w += ki * eps / fs
        phi += (w + kp * eps) / fs
    return f_out, p_out


# ------------------------------------------------------------------------------------------------ evaluation
def evaluate(trk: Track, truth_f0, onset_t: float, *, conf_min: float = 0.5, lock_cents: float = 50.0,
             gross_cents: float = 600.0, lock_frames: int = 3, max_lag_s: float = 0.25, lag_step_s: float = 0.0005,
             moving_cents_per_s: float = 500.0) -> dict:
    """Score a track against `truth_f0(t_array) -> Hz` (NaN where no note sounds) from `onset_t` on.

    lag_s      shift of the truth that best explains the track, fitted where the truth moves faster than
               `moving_cents_per_s` (0 when it does not move); every other figure uses the lag-compensated truth
    lock_time_s  first frame of `lock_frames` in a row within `lock_cents`, from the onset
    cents_*    median / 95th percentile of |error| from the lock on
    octave_errors  frames with confidence >= conf_min whose error exceeds `gross_cents`
    conf_open_s    first frame with confidence >= conf_min, from the onset"""
    nan = math.nan
    keep = np.asarray(trk.t, dtype=float) >= onset_t - 1e-12
    t = np.asarray(trk.t, dtype=float)[keep]
    f = np.asarray(trk.f0, dtype=float)[keep]
    conf = np.asarray(trk.conf, dtype=float)[keep]
    res = dict(cents_median=nan, cents_p95=nan, octave_errors=0, lock_time_s=nan, lag_s=0.0, conf_open_s=nan,
               n_frames=int(t.size))
    if t.size == 0:
        return res

    def error_cents(lag):
        truth = np.asarray(truth_f0(np.maximum(t - lag, onset_t)), dtype=float)
        ok = np.isfinite(truth) & (truth > 0.0) & np.isfinite(f) & (f > 0.0)
        e = np.full(t.shape, nan)
        e[ok] = 1200.0 * np.log2(f[ok] / truth[ok])
        return e

    h = 0.005
    before, after = (np.asarray(truth_f0(t + s * h), dtype=float) for s in (-1.0, 1.0))
    with np.errstate(all="ignore"):
        rate = np.abs(1200.0 * np.log2(after / before)) / (2.0 * h)
    moving = np.isfinite(rate) & (rate >= moving_cents_per_s)
    lag = 0.0
    if moving.sum() >= 5:
        best = math.inf
        for tau in np.arange(0.0, max_lag_s + 1e-12, lag_step_s):
            e = np.abs(error_cents(tau)[moving])
            e = e[np.isfinite(e)]
            if e.size >= 5 and np.mean(e) < best - 1e-9:
                best, lag = float(np.mean(e)), float(tau)
    res["lag_s"] = lag

    e = error_cents(lag)
    good = np.isfinite(e) & (np.abs(e) <= lock_cents)
    start = next((i for i in range(len(good) - lock_frames + 1) if good[i:i + lock_frames].all()), None)
    if start is not None:
        res["lock_time_s"] = float(t[start] - onset_t)
    post = np.abs(e[(start or 0):])
    post = post[np.isfinite(post)]
    if post.size:
        res["cents_median"] = float(np.median(post))
        res["cents_p95"] = float(np.percentile(post, 95))
    res["octave_errors"] = int(np.sum(np.isfinite(e) & (conf >= conf_min) & (np.abs(e) > gross_cents)))
    opened = np.flatnonzero(conf >= conf_min)
    if opened.size:
        res["conf_open_s"] = float(t[opened[0]] - onset_t)
    return res


# ------------------------------------------------------------------------------------------------ oscillator
def f0_per_sample(trk: Track, n: int, fs: float, *, lookahead_s: float = 0.0, interp: str = "log"):
    """Per-sample f0 (Hz) for the oscillator: output sample i reads the track at i/fs + lookahead_s. 'log'
    interpolates log f between frames (Mix/Print, with lookahead_s >= the window delay); 'hold' takes the last
    frame at or before that time (causal, Live). NaN frames take the previous valid value; before the first valid
    frame there is no estimate yet: NaN (D8 M13: it used to take the first estimate from the future)."""
    t_query = np.arange(n) / fs + lookahead_s
    f = np.asarray(trk.f0, dtype=float)
    ok = np.isfinite(f) & (f > 0.0)
    if n == 0 or not ok.any():
        return np.full(n, math.nan)
    first = int(np.argmax(ok))
    idx = np.where(ok, np.arange(len(f)), -1)
    np.maximum.accumulate(idx, out=idx)
    filled = f[np.maximum(idx, first)]
    if interp == "hold":
        k = np.clip(np.searchsorted(trk.t, t_query, side="right") - 1, 0, len(filled) - 1)
        out = filled[k]
    else:
        out = np.exp(np.interp(t_query, trk.t[first:], np.log(filled[first:])))
    out[t_query < trk.t[first]] = math.nan
    return out


def shape_wave(phi, shape_pct: float):
    """(1 - s) sin(phi) + s tri(phi), s = shape/100; tri = (2/pi) asin(sin phi) is the naive triangle (unit peak)."""
    s = shape_pct / 100.0
    return (1.0 - s) * np.sin(phi) + s * (2.0 / math.pi) * np.arcsin(np.sin(phi))


def oscillator(f0_per_sample, fs: float, shape_pct: float, phase0: float = 0.0):
    """Phase-accumulator oscillator at the given frequency (Hz, per sample); pass f0/2 for Sub -1, f0/4 for Sub -2."""
    f = np.asarray(f0_per_sample, dtype=np.float64)
    phi = phase0 + 2.0 * math.pi * np.concatenate([[0.0], np.cumsum(f[:-1])]) / fs
    return shape_wave(phi, shape_pct)
