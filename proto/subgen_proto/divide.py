# SPDX-License-Identifier: AGPL-3.0-or-later
"""DIVIDE engine (SPEC §6.2): octave divider by sign inversion at zero crossings.

y1 = x * s1 where s1 flips at every (armed) rising zero crossing of x -> period 2T, frequency f/2.
y2 = y1 * s2 where s2 flips at every second (armed) falling crossing of x -> period 4T, frequency f/4.
All flips happen at zero crossings of x, so |dy| never exceeds |dx| (continuity). Event based: candidate crossings
are found vectorised, arming (a dip below -theta since the last fire) uses cumulative counts, and only the events
are visited in Python.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import ndimage, signal as sps

from . import envelope, svf

FLOOR = 10.0 ** (-90.0 / 20.0)  # arming floor, -90 dBFS
PROVISIONAL_CONF = 0.0           # confidence while no period has been compared yet (SPEC-literal: gate stays shut)
MIN_GAP_S = 0.04                 # silence longer than max(2.5 periods, 40 ms) starts a new note


@dataclass
class DivideResult:
    y1: np.ndarray
    y2: np.ndarray
    f_est: np.ndarray  # Hz per sample (0 where unknown)
    conf: np.ndarray   # 0..1 per sample
    rising: np.ndarray   # fractional sample positions of fired rising crossings
    falling: np.ndarray  # fractional sample positions of fired falling crossings


def divide(x, fs: float, *, live: bool = False, conf_mode: str = "trend") -> DivideResult:
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    hyst = 0.03 if live else 0.05
    e_pk = envelope.peak_follower(x, fs, 1.0, 50.0)
    theta = np.maximum(hyst * e_pk, FLOOR)
    cs_neg = np.cumsum(x < -theta)
    cs_pos = np.cumsum(x > theta)

    rise_c = np.flatnonzero((x[:-1] < 0.0) & (x[1:] >= 0.0)) + 1
    fall_c = np.flatnonzero((x[:-1] > 0.0) & (x[1:] <= 0.0)) + 1
    cand = np.concatenate([rise_c, fall_c])
    is_fall = np.concatenate([np.zeros(len(rise_c), bool), np.ones(len(fall_c), bool)])
    order = np.argsort(cand, kind="stable")
    cand, is_fall = cand[order], is_fall[order]
    min_gap = MIN_GAP_S * fs

    ops: list[tuple[str, int]] = []
    store: list[tuple[int, float, float, float | None]] = []
    rising_t: list[float] = []
    falling_t: list[float] = []
    last_r = last_f = -1
    last_idx = None
    last_t = None
    s1 = 1
    pending = False
    seg_t: list[float] = []
    t_hist: list[float] = []
    r_hist: list[float] = []
    jumps: list[float] = []
    f_s = None
    period_prev = None

    for idx, fall in zip(cand.tolist(), is_fall.tolist()):
        if e_pk[idx] < 3.0 * FLOOR:
            continue
        if fall:
            base = cs_pos[last_f] if last_f >= 0 else 0
            if cs_pos[idx - 1] - base <= 0:
                continue
        else:
            base = cs_neg[last_r] if last_r >= 0 else 0
            if cs_neg[idx - 1] - base <= 0:
                continue
        t = (idx - 1) + x[idx - 1] / (x[idx - 1] - x[idx])
        if last_t is not None:
            gap = max(2.5 * period_prev, min_gap) if period_prev else min_gap
            if t - last_t > gap:  # silence: the next note starts from +1 / +1
                ops.append(("reset", last_idx + 1))
                s1 = 1
                pending = False
                period_prev = None
                seg_t, t_hist, r_hist, jumps, f_s = [], [], [], [], None
        if fall:
            last_f = idx
            falling_t.append(t)
            if pending:
                ops.append(("flip2", idx))
                pending = False
        else:
            last_r = idx
            rising_t.append(t)
            ops.append(("flip1", idx))
            s1 = -s1
            if s1 == 1:
                pending = True
            seg_t.append(t)
            f_val = c_val = 0.0
            period = None
            if len(seg_t) >= 2:
                period = seg_t[-1] - seg_t[-2]
                t_hist.append(period)
                f_med = fs / float(np.median(t_hist[-3:]))
                if f_s is None:
                    f_s = f_med
                else:
                    f_s += (1.0 - np.exp(-2.0 * np.pi * 5.0 * period / fs)) * (f_med - f_s)
                # Irregularity r of the newest period. 'diff' is SPEC §6.2 literally (change from the previous period);
                # 'trend' measures the deviation from the linear period trend 2*T[-2]-T[-3], so a smooth glide
                # (a kick's pitch drop, 12-20 % per cycle) is regular while polyphony / noise is not.
                n_p = len(t_hist)
                if conf_mode == "diff":
                    r = abs(t_hist[-1] - t_hist[-2]) / t_hist[-2] if n_p >= 2 else None
                elif n_p >= 3:
                    r = abs(t_hist[-1] - (2.0 * t_hist[-2] - t_hist[-3])) / t_hist[-2]
                elif n_p == 2:
                    r = abs(t_hist[-1] - t_hist[-2]) / t_hist[-2] / 2.0  # only two periods: allow twice the change
                else:
                    r = None
                if r is not None:
                    r_hist.append(r)
                    if r > 0.15:
                        jumps.append(t)
                if not r_hist:
                    c_val = PROVISIONAL_CONF
                elif conf_mode == "diff":  # SPEC §6.2 literally: mean of 3 cycles, then the >= 3 jumps / 1 s rule
                    c_val = float(np.mean([1.0 - min(1.0, v / 0.15) for v in r_hist[-3:]]))
                    jumps = [tj for tj in jumps if t - tj <= fs]
                    if len(jumps) >= 3:
                        c_val = 0.0
                else:  # 'trend': median irregularity of the last 5 periods (robust to single outliers)
                    c_val = 1.0 - min(1.0, float(np.median(r_hist[-5:])) / 0.15)
                f_val = f_s
                period_prev = period
            store.append((idx, f_val, c_val, period))
        last_idx, last_t = idx, t

    s1_arr = np.ones(n)
    s2_arr = np.ones(n)
    cur1 = cur2 = 1.0
    p1 = p2 = 0
    for kind, i in ops:
        if kind == "flip1":
            s1_arr[p1:i] = cur1
            p1, cur1 = i, -cur1
        elif kind == "flip2":
            s2_arr[p2:i] = cur2
            p2, cur2 = i, -cur2
        else:
            s1_arr[p1:i] = cur1
            s2_arr[p2:i] = cur2
            p1 = p2 = i
            cur1 = cur2 = 1.0
    s1_arr[p1:] = cur1
    s2_arr[p2:] = cur2

    f_arr = np.zeros(n)
    c_arr = np.zeros(n)
    for j, (idx, f_val, c_val, period) in enumerate(store):
        end = idx + int(max(2.5 * period, min_gap) if period else min_gap)
        if j + 1 < len(store):
            end = min(end, store[j + 1][0])
        end = min(end, n)
        f_arr[idx:end] = f_val
        c_arr[idx:end] = c_val

    y1 = x * s1_arr
    return DivideResult(y1, y1 * s2_arr, f_arr, c_arr, np.array(rising_t), np.array(falling_t))


def smooth_sub(res: DivideResult, fs: float, which: int) -> np.ndarray:
    """Sine-like sub: 4th-order Butterworth low-pass tracking 0.87*f_est (Sub 1) or 0.87*f_est/2 (Sub 2),
    cutoff slewed in the log domain with a 5 ms time constant."""
    y = res.y1 if which == 1 else res.y2
    valid = res.f_est > 0.0
    if not valid.any():
        return y.copy()
    idx = np.where(valid, np.arange(len(y)), 0)
    np.maximum.accumulate(idx, out=idx)
    fc = (0.87 * res.f_est * (1.0 if which == 1 else 0.5))[idx]
    first = int(np.argmax(valid))
    fc[:first] = fc[first]
    a = 1.0 - np.exp(-1.0 / (0.005 * fs))
    log_fc = np.log(fc)
    log_fc = sps.lfilter([a], [1.0, -(1.0 - a)], log_fc, zi=[(1.0 - a) * log_fc[0]])[0]
    g = np.tan(np.pi * np.exp(log_fc) / fs)
    q1 = 1.0 / (2.0 * np.sin(np.pi / 8.0))
    q2 = 1.0 / (2.0 * np.sin(3.0 * np.pi / 8.0))
    return svf.process_varying(svf.process_varying(y, g, 1.0 / q1, "low"), g, 1.0 / q2, "low")


def shape_blend(y_native, y_smooth, shape_pct: float) -> np.ndarray:
    """shape = 0 % -> smooth sine, 100 % -> native shape."""
    s = shape_pct / 100.0
    return s * np.asarray(y_native) + (1.0 - s) * np.asarray(y_smooth)


def gate_mask(conf, conf_gate_pct: float) -> np.ndarray:
    """Gate decision with hysteresis: opens at conf >= 0.2 + 0.6*conf_gate/100, closes below that minus 0.2."""
    hi = 0.2 + 0.6 * conf_gate_pct / 100.0
    lo = hi - 0.2
    conf = np.asarray(conf, dtype=np.float64)
    if conf.size == 0:
        return np.zeros(0, dtype=bool)
    starts = np.concatenate([[0], np.flatnonzero(np.diff(conf) != 0.0) + 1])
    state = False
    states = np.empty(len(starts), dtype=bool)
    for i, v in enumerate(conf[starts].tolist()):
        if not state and v >= hi:
            state = True
        elif state and v < lo:
            state = False
        states[i] = state
    return np.repeat(states, np.diff(np.concatenate([starts, [conf.size]])))


def apply_confidence_gate(y, conf, conf_gate_pct: float, fs: float) -> np.ndarray:
    """Multiply by the gate (see gate_mask); the gain follows with 5 ms attack and 50 ms release."""
    gain = envelope.peak_follower(gate_mask(conf, conf_gate_pct).astype(np.float64), fs, 5.0, 50.0)
    return np.asarray(y) * gain


def constant_level(y, x, fs: float, f_ref: float, level_db: float = -12.0) -> np.ndarray:
    """'Constant' envelope mode: divide by the causal sliding peak of |x| over 1.25 periods of f_ref, so the
    result never exceeds level_db and sits on it in steady state (a 1 ms-attack peak follower overshoots)."""
    w = max(int(np.ceil(1.25 * fs / f_ref)), 1)
    env = ndimage.maximum_filter1d(np.abs(np.asarray(x, dtype=np.float64)), size=w, origin=(w - 1) // 2)
    gain = np.where(env > 1e-6, 10.0 ** (level_db / 20.0) / np.maximum(env, 1e-6), 0.0)
    return np.asarray(y) * gain


def phase_halving(x, fs: float) -> np.ndarray:
    """Analytic-signal variant: y = |z| cos(phi/2 + c0), c0 in {0, pi} chosen so the first strong sample is co-phase."""
    x = np.asarray(x, dtype=np.float64)
    z = sps.hilbert(x)
    y = np.abs(z) * np.cos(np.unwrap(np.angle(z)) / 2.0)
    j = int(np.argmax(np.abs(x) >= 0.5 * np.max(np.abs(x))))
    return -y if np.sign(y[j]) != np.sign(x[j]) else y
