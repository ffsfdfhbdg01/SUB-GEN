# SPDX-License-Identifier: AGPL-3.0-or-later
"""S4 activity and note boundaries (SPEC §5.4, §6.2).

activity     the source band's RMS (20 ms) against det_thresh: on at the threshold, off once it has stayed 3 dB lower
             for longer than the 20 ms hold
note_starts  rising edges of the activity plus transient onsets: the fast envelope (attack 0.2 ms, release 15 ms)
             rises 12 dB above the slow one as it was 5 ms earlier (RMS over 50 ms, times sqrt 2 = the peak of a
             steady sine; the delay keeps the reference from catching up with a loud hit) while it is above
             det_thresh; a 50 ms lockout merges the two kinds of start. Each start has a detection time (causal,
             what Live can act on) and a begin time: the last sample before it at which the fast envelope was
             within 1 dB of that reference, i.e. where the rise began (what Mix/Print reach with their lookahead)
note_ends    falling edges of the activity
DIVIDE resets its polarity at these boundaries (D8 C2): narrowband noise, hum and decaying tails between hits do not
leave the flip-flop in a random state, and a new hit over an overlapping tail still starts from +1.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from . import envelope

RMS_MS, HYST_DB, HOLD_MS = 20.0, 3.0, 20.0
FAST_ATTACK_MS, FAST_RELEASE_MS, SLOW_RMS_MS, SLOW_DELAY_MS = 0.2, 15.0, 50.0, 5.0
ONSET_RATIO_DB, BEGIN_RATIO_DB, LOCKOUT_MS = 12.0, 1.0, 50.0


@dataclass(frozen=True)
class NoteStarts:
    detect: np.ndarray  # fractional sample times at which a causal detector knows the note has started
    begin: np.ndarray   # sample times at which the rise began (<= detect)

    def __len__(self) -> int:
        return len(self.detect)


def activity(x, fs: float, *, thresh_db: float = -55.0) -> np.ndarray:
    """Boolean S4 activity per sample."""
    x = np.asarray(x, dtype=np.float64)
    out = np.zeros(len(x), dtype=bool)
    if len(x) == 0:
        return out
    e = envelope.rms_follower(x, fs, RMS_MS)
    on_lvl = 10.0 ** (thresh_db / 20.0)
    off_lvl = 10.0 ** ((thresh_db - HYST_DB) / 20.0)
    hold = int(round(HOLD_MS * 1e-3 * fs))
    state, below = False, 0
    for i, v in enumerate(e.tolist()):
        if not state:
            if v >= on_lvl:
                state, below = True, 0
        elif v < off_lvl:
            below += 1
            if below > hold:
                state = False
        else:
            below = 0
        out[i] = state
    return out


def _rising(mask) -> np.ndarray:
    m = np.asarray(mask, dtype=bool)
    if len(m) == 0:
        return np.zeros(0, dtype=int)
    return np.flatnonzero(np.concatenate([[m[0]], m[1:] & ~m[:-1]]))


def note_starts(x, fs: float, *, thresh_db: float = -55.0) -> NoteStarts:
    """Note starts (activity rising edges and transient onsets), lockout applied."""
    x = np.asarray(x, dtype=np.float64)
    if len(x) == 0:
        return NoteStarts(np.zeros(0), np.zeros(0))
    e_f = envelope.peak_follower(x, fs, FAST_ATTACK_MS, FAST_RELEASE_MS)
    e_s = np.sqrt(2.0) * envelope.rms_follower(x, fs, SLOW_RMS_MS)
    k = min(int(round(SLOW_DELAY_MS * 1e-3 * fs)), len(x))
    e_s = np.concatenate([np.zeros(k), e_s[:len(x) - k]])  # the slow level as it was before the rise
    d = e_f - 10.0 ** (ONSET_RATIO_DB / 20.0) * e_s
    cond = (d >= 0.0) & (e_f >= np.sqrt(2.0) * 10.0 ** (thresh_db / 20.0))
    quiet = e_f < 10.0 ** (BEGIN_RATIO_DB / 20.0) * e_s
    last_quiet = np.where(quiet, np.arange(len(x)), -1)
    np.maximum.accumulate(last_quiet, out=last_quiet)
    events = []  # (detect, begin)
    for i in _rising(cond).tolist():
        t = (i - 1) + d[i - 1] / (d[i - 1] - d[i]) if i > 0 and d[i - 1] < 0.0 <= d[i] else float(i)
        events.append((t, float(last_quiet[i] + 1)))
    events += [(float(i), float(i)) for i in _rising(activity(x, fs, thresh_db=thresh_db)).tolist()]
    det, beg, last = [], [], -np.inf
    lockout = LOCKOUT_MS * 1e-3 * fs
    for t, b in sorted(events):
        if t - last >= lockout:
            det.append(t)
            beg.append(min(b, t))
            last = t
    return NoteStarts(np.array(det), np.array(beg))


def note_ends(x, fs: float, *, thresh_db: float = -55.0) -> np.ndarray:
    """Sample indices at which the activity falls."""
    a = activity(x, fs, thresh_db=thresh_db)
    if len(a) < 2:
        return np.zeros(0, dtype=int)
    return np.flatnonzero(a[:-1] & ~a[1:]) + 1
