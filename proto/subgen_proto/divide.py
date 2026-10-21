# SPDX-License-Identifier: AGPL-3.0-or-later
"""DIVIDE engine (SPEC §6.2): octave divider by sign inversion at zero crossings.

y1 = x * s1 where s1 flips at every (armed) rising zero crossing of x -> period 2T, frequency f/2.
y2 = y1 * s2 where s2 flips at every second (armed) falling crossing of x -> period 4T, frequency f/4.
All flips happen at zero crossings of x, so |dy| never exceeds |dx| (continuity). Event based: candidate crossings
are found vectorised, arming (a dip below -theta since the last fire) uses cumulative counts, and only the events
are visited in Python.

Note boundaries (D8 C1/C2, R1/R2; activity.py): the divider runs from a note start to the next note end (S4
activity falling) and is frozen in between, so noise, hum or a decaying tail cannot leave s1/s2 in a random state.
Polarity reset to +1/+1 with the arming cleared:
  note end          at the next zero crossing (continuous)
  note start, Mix/Print  at the last zero crossing before the rise began, reachable with the lookahead (continuous)
  note start, Live  at the detection, with a 0.5 ms ramp (only matters inside a running note: overlapping hits)
From a note start the confidence is provisionally 1 until three periods give the first trend residual; the gate
keeps a provisional opening only if that first residual reaches the opening threshold.
shape (R5, D8 I4) moves the tracking low-pass from 0.87 f_est (sine) up to 0.87 * 32 f_est (practically native)
instead of mixing the native and the smoothed path, which are ~91 degrees apart at f/2 (-3.1 dB at 50 %).
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import signal as sps

from . import activity, envelope, svf

FLOOR = 10.0 ** (-90.0 / 20.0)  # arming floor, -90 dBFS
PROVISIONAL_CONF = 1.0           # confidence from a note start until the first residual (R1; SPEC 0.1/0.2: 0)
RAMP_MS = 0.5                    # Live: polarity reset at a note start detected inside a running note
MIN_GAP_S = 0.04                 # hold of a confidence value while no period is known


@dataclass
class DivideResult:
    y1: np.ndarray
    y2: np.ndarray
    f_est: np.ndarray  # Hz per sample (0 where unknown)
    conf: np.ndarray   # 0..1 per sample
    rising: np.ndarray   # fractional sample positions of fired rising crossings
    falling: np.ndarray  # fractional sample positions of fired falling crossings
    resets: np.ndarray   # sample indices of polarity resets (note starts and ends)
    provisional: np.ndarray  # True where conf is the provisional value of a note start


def divide(x, fs: float, *, live: bool = False, conf_mode: str = "trend", lookahead_s: float = 0.045,
           thresh_db: float = -55.0) -> DivideResult:
    """lookahead_s: how far ahead the engine sees (Mix 0.045, Print 0.055); 0 = Live placement of the start reset.
    thresh_db: det_thresh of the S4 activity that bounds the notes."""
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    if n == 0:
        z = np.zeros(0)
        return DivideResult(z, z.copy(), z.copy(), z.copy(), z.copy(), z.copy(), np.zeros(0, dtype=int),
                            np.zeros(0, dtype=bool))
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
    look = int(round(lookahead_s * fs))
    ramp = max(1, int(round(RAMP_MS * 1e-3 * fs)))

    starts = activity.note_starts(x, fs, thresh_db=thresh_db)
    events = []  # (processing time, kind, detection time)
    for t_det, t_beg in zip(starts.detect.tolist(), starts.begin.tolist()):
        events.append((t_det if look == 0 else max(t_beg, t_det - look), "start", t_det))
    events += [(float(e), "end", float(e)) for e in activity.note_ends(x, fs, thresh_db=thresh_db).tolist()]
    events.sort()

    ops: list[tuple[str, int]] = []
    store: list[tuple[int, float, float, float | None, bool]] = []
    rising_t: list[float] = []
    falling_t: list[float] = []
    resets: list[int] = []
    st = dict(last_r=-1, last_f=-1, s1=1, pending=False, running=False, provisional=False, end_pending=False,
              seg_t=[], t_hist=[], r_hist=[], jumps=[], f_s=None, period_prev=None)

    def clear(k: int, kind: str, conf_after: float) -> None:
        ops.append((kind, k))
        resets.append(k)
        store.append((k, 0.0, conf_after, None, conf_after == PROVISIONAL_CONF))
        st.update(last_r=k - 1, last_f=k - 1, s1=1, pending=False, seg_t=[], t_hist=[], r_hist=[], jumps=[],
                  f_s=None, period_prev=None)

    def handle(ev) -> None:
        t, kind, t_det = ev
        if kind == "end":
            st["end_pending"] = True
            return
        st["end_pending"] = False
        if look == 0:
            clear(min(int(np.ceil(t_det)), n - 1), "ramp", PROVISIONAL_CONF)
        else:
            j = int(np.searchsorted(cand, t, side="right")) - 1
            k = int(cand[j]) if j >= 0 and cand[j] >= t_det - look else int(t)
            clear(k, "switch", PROVISIONAL_CONF)
        st["running"] = True
        st["provisional"] = True

    ev_i = 0
    for idx, fall in zip(cand.tolist(), is_fall.tolist()):
        while ev_i < len(events) and events[ev_i][0] < idx:
            handle(events[ev_i])
            ev_i += 1
        if st["end_pending"]:  # the note ended: reset at this crossing, then stay frozen
            clear(idx, "switch", 0.0)
            st.update(running=False, provisional=False, end_pending=False)
            continue
        if not st["running"]:
            continue
        if fall:
            base = cs_pos[st["last_f"]] if st["last_f"] >= 0 else 0
            if cs_pos[idx - 1] - base <= 0:
                continue
        else:
            base = cs_neg[st["last_r"]] if st["last_r"] >= 0 else 0
            if cs_neg[idx - 1] - base <= 0:
                continue
        t = (idx - 1) + x[idx - 1] / (x[idx - 1] - x[idx])
        if fall:
            st["last_f"] = idx
            falling_t.append(t)
            if st["pending"]:
                ops.append(("flip2", idx))
                st["pending"] = False
            continue
        st["last_r"] = idx
        rising_t.append(t)
        ops.append(("flip1", idx))
        st["s1"] = -st["s1"]
        if st["s1"] == 1:
            st["pending"] = True
        seg_t, t_hist, r_hist = st["seg_t"], st["t_hist"], st["r_hist"]
        seg_t.append(t)
        f_val = 0.0
        c_val = PROVISIONAL_CONF if st["provisional"] else 0.0
        prov = st["provisional"]
        period = None
        if len(seg_t) >= 2:
            period = seg_t[-1] - seg_t[-2]
            t_hist.append(period)
            f_med = fs / float(np.median(t_hist[-3:]))
            if st["f_s"] is None:
                st["f_s"] = f_med
            else:
                st["f_s"] += (1.0 - np.exp(-2.0 * np.pi * 5.0 * period / fs)) * (f_med - st["f_s"])
            # Irregularity r of the newest period. 'diff' is SPEC §6.2 0.1 literally (change from the previous
            # period); 'trend' measures the deviation from the linear period trend 2*T[-2]-T[-3], so a smooth glide
            # (a kick's pitch drop, 12-20 % per cycle) is regular while polyphony / noise is not.
            n_p = len(t_hist)
            if conf_mode == "diff":
                r = abs(t_hist[-1] - t_hist[-2]) / t_hist[-2] if n_p >= 2 else None
            else:
                r = abs(t_hist[-1] - (2.0 * t_hist[-2] - t_hist[-3])) / t_hist[-2] if n_p >= 3 else None
            if r is not None:
                r_hist.append(r)
                if r > 0.15:
                    st["jumps"].append(t)
            if r_hist:
                prov = st["provisional"] = False
                if conf_mode == "diff":  # SPEC §6.2 0.1 literally: mean of 3 cycles, then the >= 3 jumps / 1 s rule
                    c_val = float(np.mean([1.0 - min(1.0, v / 0.15) for v in r_hist[-3:]]))
                    st["jumps"] = [tj for tj in st["jumps"] if t - tj <= fs]
                    if len(st["jumps"]) >= 3:
                        c_val = 0.0
                else:  # 'trend': median irregularity of the last 5 periods (robust to single outliers)
                    c_val = 1.0 - min(1.0, float(np.median(r_hist[-5:])) / 0.15)
            f_val = st["f_s"]
            st["period_prev"] = period
        store.append((idx, f_val, c_val, period, prov))
    while ev_i < len(events):  # events after the last crossing
        handle(events[ev_i])
        ev_i += 1

    s1_arr = np.empty(n)
    s2_arr = np.empty(n)
    cur1 = cur2 = 1.0
    p1 = p2 = 0
    for kind, i in ops:
        i = max(i, p1 if kind != "flip2" else p2)
        if kind == "flip1":
            s1_arr[p1:i] = cur1
            p1, cur1 = i, -cur1
        elif kind == "flip2":
            s2_arr[p2:i] = cur2
            p2, cur2 = i, -cur2
        else:
            i = max(i, p1, p2)
            s1_arr[p1:i] = cur1
            s2_arr[p2:i] = cur2
            j = i
            if kind == "ramp":
                j = min(i + ramp, n)
                frac = np.arange(1, j - i + 1) / ramp
                s1_arr[i:j] = cur1 + (1.0 - cur1) * frac
                s2_arr[i:j] = cur2 + (1.0 - cur2) * frac
            p1 = p2 = j
            cur1 = cur2 = 1.0
    s1_arr[p1:] = cur1
    s2_arr[p2:] = cur2

    f_arr = np.zeros(n)
    c_arr = np.zeros(n)
    p_arr = np.zeros(n, dtype=bool)
    for j, (idx, f_val, c_val, period, prov) in enumerate(store):
        end = idx + int(max(2.5 * period, min_gap) if period else min_gap)
        if j + 1 < len(store):
            end = min(end, store[j + 1][0])
        end = min(end, n)
        f_arr[idx:end] = f_val
        c_arr[idx:end] = c_val
        p_arr[idx:end] = prov

    y1 = x * s1_arr
    return DivideResult(y1, y1 * s2_arr, f_arr, c_arr, np.array(rising_t), np.array(falling_t),
                        np.array(resets, dtype=int), p_arr)


SHAPE_OCTAVES = 5.0  # shape 100 % raises the smoothing cutoff by 2^5 = 32 times


def smooth_cutoff_hz(res: DivideResult, fs: float, which: int, *, shape_pct: float = 0.0,
                     f_init: float = 120.0) -> np.ndarray:
    """Cutoff of the tracking low-pass per sample (before the 5 ms log slew): 0.87 f_est (Sub 1) or 0.87 f_est/2
    (Sub 2) times 2^(5 shape/100). Causal: f_init (range_hi) until a note's first estimate, and again from every
    polarity reset; between notes the last estimate is held."""
    n = len(res.y1)
    f = np.where(res.f_est > 0.0, res.f_est, np.nan)
    if len(res.resets):
        r = res.resets[(res.resets >= 0) & (res.resets < n)]
        f[r] = np.where(np.isnan(f[r]), f_init, f[r])
    idx = np.where(np.isfinite(f), np.arange(n), -1)
    np.maximum.accumulate(idx, out=idx)
    f_held = np.where(idx >= 0, f[np.maximum(idx, 0)], f_init)
    fc = 0.87 * f_held * (1.0 if which == 1 else 0.5) * 2.0 ** (SHAPE_OCTAVES * shape_pct / 100.0)
    return np.minimum(fc, 0.45 * fs)


def smooth_sub(res: DivideResult, fs: float, which: int, *, shape_pct: float = 0.0, f_init: float = 120.0) -> np.ndarray:
    """Sub through a 4th-order Butterworth low-pass at smooth_cutoff_hz, slewed in the log domain over 5 ms:
    shape 0 = sine-like (3f/2 >= 18 dB down), 100 = practically the native shape."""
    y = res.y1 if which == 1 else res.y2
    if len(y) == 0:
        return y.copy()
    fc = smooth_cutoff_hz(res, fs, which, shape_pct=shape_pct, f_init=f_init)
    a = 1.0 - np.exp(-1.0 / (0.005 * fs))
    log_fc = np.log(fc)
    log_fc = sps.lfilter([a], [1.0, -(1.0 - a)], log_fc, zi=[(1.0 - a) * log_fc[0]])[0]
    g = np.tan(np.pi * np.exp(log_fc) / fs)
    q1 = 1.0 / (2.0 * np.sin(np.pi / 8.0))
    q2 = 1.0 / (2.0 * np.sin(3.0 * np.pi / 8.0))
    return svf.process_varying(svf.process_varying(y, g, 1.0 / q1, "low"), g, 1.0 / q2, "low")


REJECT_RELEASE_MS = 10.0  # a provisional opening that the first real confidence does not confirm closes fast


def gate_states(conf, conf_gate_pct: float, provisional=None):
    """(open, fast) per sample: the gate decision of gate_mask, and True while the gate is closed after a rejected
    provisional opening (it then releases over 10 ms instead of 50 ms)."""
    hi = 0.2 + 0.6 * conf_gate_pct / 100.0
    lo = max(hi - 0.2, 0.5 * hi)
    conf = np.asarray(conf, dtype=np.float64)
    if conf.size == 0:
        return np.zeros(0, dtype=bool), np.zeros(0, dtype=bool)
    prov = np.zeros(conf.size, dtype=bool) if provisional is None else np.asarray(provisional, dtype=bool)
    change = (np.diff(conf) != 0.0) | (np.diff(prov.astype(np.int8)) != 0)
    starts = np.concatenate([[0], np.flatnonzero(change) + 1])
    state, was_prov, fast = False, False, False
    states = np.empty(len(starts), dtype=bool)
    fasts = np.empty(len(starts), dtype=bool)
    for i, (v, pv) in enumerate(zip(conf[starts].tolist(), prov[starts].tolist())):
        if pv:
            state, was_prov, fast = True, True, False
        elif was_prov:
            state, was_prov = v >= hi, False
            fast = not state
        elif not state and v >= hi:
            state, fast = True, False
        elif state and v < lo:
            state = False
        states[i], fasts[i] = state, fast
    reps = np.diff(np.concatenate([starts, [conf.size]]))
    return np.repeat(states, reps), np.repeat(fasts, reps)


def gate_mask(conf, conf_gate_pct: float, provisional=None) -> np.ndarray:
    """Gate decision with hysteresis: opens at conf >= hi = 0.2 + 0.6*conf_gate/100, closes below
    max(hi - 0.2, hi / 2) (so conf_gate 0 still closes, below 0.1). Provisional samples hold the gate open; when
    they end, the first real value must reach hi to keep it open."""
    if np.asarray(conf).size == 0:
        return np.zeros(0, dtype=bool)
    return gate_states(conf, conf_gate_pct, provisional)[0]


def apply_confidence_gate(y, conf, conf_gate_pct: float, fs: float, provisional=None) -> np.ndarray:
    """Multiply by the gate (see gate_mask); the gain follows with 5 ms attack and 50 ms release (10 ms after a
    rejected provisional opening)."""
    y = np.asarray(y)
    if y.size == 0:
        return y.copy()
    open_, fast = gate_states(conf, conf_gate_pct, provisional)
    a_att = 1.0 - np.exp(-1.0 / (5e-3 * fs))
    a_rel = 1.0 - np.exp(-1.0 / (50e-3 * fs))
    a_fast = 1.0 - np.exp(-1.0 / (REJECT_RELEASE_MS * 1e-3 * fs))
    gain = np.empty(len(open_))
    g = 0.0
    for i, (o, f) in enumerate(zip(open_.tolist(), fast.tolist())):
        target = 1.0 if o else 0.0
        g += (a_att if target > g else (a_fast if f else a_rel)) * (target - g)
        gain[i] = g
    return y * gain


def constant_level(y, x, fs: float, f_ref: float, level_db: float = -12.0) -> np.ndarray:
    """'Constant' envelope mode: divide by the causal sliding peak of |x| over 1.25 periods of f_ref, so the
    result never exceeds level_db and sits on it in steady state (a 1 ms-attack peak follower overshoots)."""
    env = envelope.sliding_peak(np.abs(x), max(int(np.ceil(1.25 * fs / f_ref)), 1))
    gain = np.where(env > 1e-6, 10.0 ** (level_db / 20.0) / np.maximum(env, 1e-6), 0.0)
    return np.asarray(y) * gain


def phase_halving(x, fs: float) -> np.ndarray:
    """Analytic-signal variant: y = |z| cos(phi/2 + c0), c0 in {0, pi} chosen so the first strong sample is co-phase."""
    x = np.asarray(x, dtype=np.float64)
    z = sps.hilbert(x)
    y = np.abs(z) * np.cos(np.unwrap(np.angle(z)) / 2.0)
    j = int(np.argmax(np.abs(x) >= 0.5 * np.max(np.abs(x))))
    return -y if np.sign(y[j]) != np.sign(x[j]) else y
