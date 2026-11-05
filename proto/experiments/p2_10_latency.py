# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-10: latency budget (SPEC §4.4) from the P2-03 (TRACK onset), P2-06 (oversampling), P2-08 (Linear Phase) and
P2-09 (limiter) results: L_sub = L_os + L_look + L_lin, L_total = L_sub + round(align_ms fs / 1000).
Re-run after D8: Linear Phase for the low-pass only with N_LIN + a 256-sample partition (SPEC 0.2 §7.6) and the natural
sub-path delay on the default chain (D8 M2); the SPEC 0.1-literal checks are kept as information."""
import json
import math

import numpy as np

from experiments._common import REPO_ROOT, Recorder
from subgen_proto import drive, lfe, svf

SPEC = {"Live": 0.0, "Mix": 25.0, "Print": 50.0}  # SPEC §4.4 L_look [ms] ([P2])
COMP_LOOKAHEAD_MS = {"Live": 0.0, "Mix": 0.0, "Print": 5.0}  # SPEC §7.4: up to 5 ms in Mix/Print
LIMITER_LOOKAHEAD_MS = {"Live": 0.0, "Mix": 2.0, "Print": 5.0}  # SPEC §7.8


def load(name):
    return json.loads((REPO_ROOT / "proto" / "out" / f"{name}.json").read_text())["info"]


def main():
    rec = Recorder("p2_10")
    p03, p08 = load("p2_03"), load("p2_08")
    track_ms = p03["l_look_ms"]
    onset = {float(f): v for f, v in p03["onset after silence (through the range filter), MPM 2.5 periods: f -> lock / conf-open / excess over 2 periods"].items()}
    dec_ms = float(p03["8th-order Butterworth decimation filter at 800 Hz: group delay at 50 Hz [ms]"])

    # which notes does a lookahead cover? TRACK's confidence must be open at the onset: conf-open time + decimator delay
    def lowest_covered(l_ms):
        ok = [f for f, v in onset.items() if v["conf_open_ms"] + dec_ms <= l_ms]
        return min(ok) if ok else math.nan
    cover = {f"{k} {v:g} ms": lowest_covered(v) for k, v in SPEC.items() if v > 0}
    cover.update({f"proposed Mix {track_ms['mix']} ms": lowest_covered(track_ms["mix"]), f"proposed Print {track_ms['print']} ms": lowest_covered(track_ms["print"])})
    rec.info("lowest note whose TRACK sub starts on the dry onset, by L_look [Hz]", cover)
    rec.info("SPEC 0.1 Mix L_look (25 ms): lowest TRACK note covered [Hz] (SPEC-literal; 47 ms since SPEC 0.2)", lowest_covered(SPEC["Mix"]))
    rec.check("proposed Mix L_look covers TRACK notes down to 55 Hz (A1)", lowest_covered(track_ms["mix"]), "<=", 55.0, "Hz")
    rec.check("proposed Print L_look covers TRACK notes down to 41.2 Hz (E1)", lowest_covered(track_ms["print"]), "<=", 41.2, "Hz")

    # stacked lookahead stages add up: the engine needs L ms of the future, the compressor and limiter need theirs of the sub
    l_look = {"Live": 0.0, "Mix": track_ms["mix"] + COMP_LOOKAHEAD_MS["Mix"] + LIMITER_LOOKAHEAD_MS["Mix"],
              "Print": track_ms["print"] + COMP_LOOKAHEAD_MS["Print"] + LIMITER_LOOKAHEAD_MS["Print"]}
    rec.info("proposed L_look [ms] = TRACK + compressor + limiter lookahead", l_look)

    n_lin = {int(k): v["N_LIN"] for k, v in p08["N_LIN = 4096 * 2^ceil(log2(fs / 48 kHz)) + 1 and its latency"].items()}
    table = {}
    for fs in (44100, 48000, 96000):
        os_lat = {q: (len(drive.oversampling_taps(f)) - 1) / f for q, f in (("Mix", 2), ("Print", 4))}  # up + down FIR
        rows = {}
        for q in ("Live", "Mix", "Print", "Print + Linear Phase (LP, N_LIN + 256-sample partition)"):
            base = q.split(" ")[0]
            l_os = 0.0 if base == "Live" else os_lat[base]
            look = round(l_look[base] * fs / 1000.0)
            lin = (n_lin[fs] - 1) // 2 + 256 if "Linear" in q else 0
            total = int(math.ceil(l_os)) + look + lin
            rows[q] = {"L_os": round(l_os, 1), "L_look": look, "L_lin": lin, "L_total [samples]": total,
                       "L_total [ms]": round(1000.0 * total / fs, 1)}
        table[fs] = rows
    rec.info("L_total (align_ms = 0) by quality and sample rate; L_os is the Kaiser FIR stand-in (an upper bound: JUCE's "
             "half-band IIR for Mix is far shorter, read getLatencyInSamples() in Phase 3)", table)
    rec.info("SPEC §4.4 (for comparison) L_total [ms]", {"Live": 0, "Mix": "~25", "Print": "~50+"})

    # natural group delays of the sub path that align_ms compensates (frequency dependent, not latency)
    p07 = load("p2_07")
    gd_a = p07["group delay of chain A / B [ms] at [20.0, 30.0, 50.0, 80.0, 120.0, 200.0, 300.0] Hz (n = 4)"]["A"]
    nat = {"rotation stage (chain A, theta = 0) at 30 / 50 / 120 Hz": (gd_a[1], gd_a[2], gd_a[4]),
           "LFE low-pass BW24 @ 120 Hz at 40 / 80 / 120 Hz": tuple(round(float(v), 2) for v in 1000.0 * svf.group_delay_s(svf.design_butter(4, 120.0, 48000, "low"), [40.0, 80.0, 120.0], 48000)),
           "source band BW4 30-120 Hz at 40 / 60 / 100 Hz": tuple(round(float(v), 2) for v in 1000.0 * svf.group_delay_s(
               svf.design_butter(4, 30.0, 48000, "high") + svf.design_butter(4, 120.0, 48000, "low"), [40.0, 60.0, 100.0], 48000))}
    rec.info("natural group delays in the sub path [ms] (compensated with align_ms; frequency dependent)", nat)
    from subgen_proto import hilbert
    d4 = hilbert.design(4)
    f_sub = np.array([30.0, 40.0, 50.0, 80.0])
    gd_rot = 1000.0 * np.sum(2.0 * d4.poles_a / (d4.poles_a ** 2 + (2.0 * np.pi * f_sub)[:, None] ** 2), axis=1)
    # D8 M2: the default chain. DIVIDE/TRACK take the source at 2 f_sub through the range band; the sub then passes the
    # subsonic HP (22 Hz, 24 dB/oct), the sub LP (140 Hz, 24 dB/oct) and chain A of the rotation stage at f_sub.
    band = svf.design_butter(4, 30.0, 48000, "high") + svf.design_butter(4, 120.0, 48000, "low")
    filt = svf.design_butter(4, 22.0, 48000, "high") + svf.design_butter(4, 140.0, 48000, "low")
    total = 1000.0 * (svf.group_delay_s(band, 2.0 * f_sub, 48000) + svf.group_delay_s(filt, f_sub, 48000)) + gd_rot
    rec.info("sub path total at f_sub 30 / 40 / 50 / 80 Hz [ms], default chain: source band BW4 30-120 at 2 f_sub + HP 22 + "
             "LP 140 (BW4) + rotation chain A at f_sub", tuple(round(float(v), 1) for v in total))
    rec.info("SPEC 0.1 align_ms range (0-20 ms) at f_sub 40 Hz (SPEC-literal; 0-40 ms since SPEC 0.2): natural delay [ms]",
             round(float(total[1]), 1))
    rec.check("align_ms (0-40 ms, SPEC 0.2) covers the sub path's natural delay down to f_sub 30 Hz", float(total[0]), "<=", 40.0, "ms")
    rec.check("Live L_total is zero", table[48000]["Live"]["L_total [samples]"], "==", 0)
    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
