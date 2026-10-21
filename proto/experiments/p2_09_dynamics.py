# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-09: sub-path dynamics - compressor THD by detector / ripple rule / time constants, limiter THD, gate, auto release."""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from experiments._common import Recorder
from subgen_proto import dynamics, signals, util

FS = 48000
ATTACKS = (0.1, 1.0, 3.0, 10.0, 30.0, 100.0)
RELEASES = (10.0, 100.0)
DETECTORS = ("rms", "peak", "rms_window", "rms_onepole", "peak_instant")


def comp_thd(f, detect, attack, release, rule, f_sub1=None, gr_db=6.0):
    """THD of a -12 dBFS sine compressed by gr_db (R = 4): the threshold sits gr/0.75 dB below the detector level."""
    x = signals.sine(f, FS, 2.0, level_db=-12.0)
    level = -12.0 if detect.startswith("peak") else -12.0 + 20.0 * np.log10(np.sqrt(0.5))
    y, gr = dynamics.compressor(x, FS, thresh_db=level - gr_db / 0.75, ratio=4.0, knee_db=0.0, attack_ms=attack,
                                release_ms=release, detect=detect, f_sub1=f_sub1 or f, ripple_rule=rule)
    return util.thd_db(y[FS:2 * FS], FS, f), float(np.median(gr[FS:2 * FS]))


def main():
    rec = Recorder("p2_09")

    # ---------------------------------------------------------------- 1. compressor THD map (T-CMP-02)
    thd = {}
    for det in DETECTORS:
        for rule in (False, True):
            for f in (30.0, 50.0, 100.0):
                for a in ATTACKS:
                    for r in RELEASES:
                        thd[(det, rule, f, a, r)] = comp_thd(f, det, a, r, rule)[0]
    worst = {f"{det} | rule {'on' if rule else 'off'}": round(max(v for k, v in thd.items() if k[0] == det and k[1] == rule), 1)
             for det in DETECTORS for rule in (False, True)}
    rec.info("compressor THD at GR 6 dB, worst over 30/50/100 Hz x attack 0.1-100 ms x release 10/100 ms [dB]", worst)
    brief = {f"{det} | rule {'on' if rule else 'off'}": round(comp_thd(40.0, det, 1.0, 10.0, rule)[0], 1) for det in DETECTORS for rule in (False, True)}
    rec.info("40 Hz, attack 1 ms, release 10 ms (the brief's point) [dB]", brief)
    for rule in (False, True):
        rec.check(f"T-CMP-02: THD <= -60 dB at GR 6 dB, recommended 'rms' detector, rule {'on' if rule else 'off'} (whole map)",
                  max(v for k, v in thd.items() if k[0] == "rms" and k[1] == rule), "<=", -60.0, "dB")
    rec.info("T-CMP-02 with the SPEC 0.1 one-pole detector and the ripple rule (whole map) [dB] (SPEC-literal; replaced by the "
             "ripple-free detectors in SPEC 0.2)", round(max(v for k, v in thd.items() if k[0] == "rms_onepole" and k[1]), 1))

    # off-frequency: the window is tuned to f_sub1 = 40 Hz
    off = {}
    for f in (36.0, 40.0, 44.0, 48.0, 60.0, 80.0):
        off[f] = {det: round(comp_thd(f, det, 1.0, 10.0, True, f_sub1=40.0)[0], 1) for det in ("rms", "peak", "rms_window")}
    rec.info("off-frequency tones with f_sub1 = 40 Hz (attack 1 / release 10 ms, rule on): THD [dB]", off)
    rec.check("recommended 'rms' detector stays <= -60 dB from 36 to 80 Hz with f_sub1 = 40 Hz",
              max(v["rms"] for v in off.values()), "<=", -60.0, "dB")
    bias = {f: round(comp_thd(f, "rms", 10.0, 100.0, True, f_sub1=40.0)[1] + 6.0, 2) for f in (36.0, 44.0, 48.0, 60.0)}
    rec.info("'rms' with hold, off f_sub1: extra gain reduction from reading the top of the mean-square ripple [dB]", bias)

    # ---------------------------------------------------------------- 2. onset: what the ripple rule is for
    x = np.concatenate([np.zeros(FS // 2), signals.sine(40.0, FS, 1.0, level_db=-6.0)])
    onset = {}
    for rule in (False, True):
        y, gr = dynamics.compressor(x, FS, thresh_db=-30.0, ratio=4.0, knee_db=0.0, attack_ms=1.0, release_ms=10.0,
                                    detect="peak", f_sub1=40.0, ripple_rule=rule)
        seg = y[FS // 2:FS // 2 + int(0.08 * FS)] * np.hanning(int(0.08 * FS))
        spec = np.abs(np.fft.rfft(seg, 2 ** 16)) ** 2
        fq = np.fft.rfftfreq(2 ** 16, 1.0 / FS)
        onset[f"rule {'on' if rule else 'off'}"] = round(10 * np.log10(np.sum(spec[fq > 100.0]) / np.sum(spec)), 1)
    rec.info("onset of a 40 Hz tone (GR 0 -> 18 dB, attack 1 ms): energy above 100 Hz in the first 80 ms [dB re total]", onset)
    sweep = {}
    for det in ("peak", "rms"):
        for a in (0.1, 1.0, 3.0, 6.25, 10.0, 30.0):
            y, _ = dynamics.compressor(x, FS, thresh_db=-30.0, ratio=4.0, knee_db=0.0, attack_ms=a, release_ms=100.0,
                                       detect=det, f_sub1=40.0, ripple_rule=False)
            seg = y[FS // 2:FS // 2 + int(0.08 * FS)] * np.hanning(int(0.08 * FS))
            spec = np.abs(np.fft.rfft(seg, 2 ** 16)) ** 2
            fq = np.fft.rfftfreq(2 ** 16, 1.0 / FS)
            sweep[f"{det}, attack {a:g} ms"] = round(10 * np.log10(np.sum(spec[fq > 100.0]) / np.sum(spec)), 1)
    rec.info("onset splatter vs attack (no ripple rule; 'rms' keeps its own floor of 0.25 T_sub = 6.25 ms at 40 Hz, R4) "
             "[dB re total]", sweep)
    rec.check("T-CMP-02 onset (R4): Peak, attack <= 3 ms, energy above 100 Hz in the first 80 ms",
              max(v for k, v in sweep.items() if k.startswith("peak") and float(k.split()[2]) <= 3.0), "<=", -50.0, "dB")
    rec.check("T-CMP-02 onset (R4): RMS (attack floor 0.25 T_sub), any attack",
              max(v for k, v in sweep.items() if k.startswith("rms")), "<=", -40.0, "dB")

    # ---------------------------------------------------------------- 3. limiter (T-LIM-01, T-LIM-02)
    c_db = -1.0
    xs = signals.sine(40.0, FS, 3.0, level_db=c_db + 6.0)
    lim = {"live (c tanh(x/c), ADAA)": round(util.thd_db(dynamics.limiter(xs, FS, ceiling_db=c_db, lookahead_ms=0.0, release_ms=100.0, live=True)[FS:2 * FS], FS, 40.0), 1)}
    for la in (2.0, 5.0):
        for rel in (50.0, 100.0, 300.0, 1000.0):
            y = dynamics.limiter(xs, FS, ceiling_db=c_db, lookahead_ms=la, release_ms=rel)
            lim[f"lookahead {la:g} ms, release {rel:g} ms"] = round(util.thd_db(y[FS:2 * FS], FS, 40.0), 1)
    rec.info("T-LIM-02: limiter THD, 40 Hz sine 6 dB over a -1 dBFS ceiling [dB]", lim)
    c = 10 ** (c_db / 20.0)
    stress = {"square +6 dB": signals.square(40.0, FS, 1.0, level_db=c_db + 6.0), "kick +9 dB": signals.kick(FS, n_hits=3, spacing=0.3, level_db=c_db + 9.0),
              "noise sigma 2": np.random.default_rng(0).standard_normal(FS) * 2.0, "808 +6 dB": signals.bass_808(FS, level_db=c_db + 6.0)}
    over = max(float(np.max(np.abs(dynamics.limiter(s, FS, ceiling_db=c_db, lookahead_ms=la, release_ms=100.0, live=(la == 0.0))))) - c
               for s in stress.values() for la in (0.0, 2.0, 5.0))
    rec.check("T-LIM-01: no output above the ceiling (4 stress signals x live / 2 ms / 5 ms)", over, "<=", 1e-9)

    # ---------------------------------------------------------------- 4. auto release and gate
    def release_ms(burst_s, auto):
        xx = np.concatenate([signals.sine(100.0, FS, burst_s, level_db=-6.0), signals.sine(100.0, FS, 2.0, level_db=-40.0)])
        _, g = dynamics.compressor(xx, FS, thresh_db=-30.0, ratio=4.0, knee_db=0.0, attack_ms=5.0, release_ms=50.0, detect="peak",
                                   f_sub1=100.0, auto_release=auto)
        n0 = int(burst_s * FS)
        return round(np.argmax(g[n0:] >= g[n0 - 1] * np.exp(-1.0)) / FS * 1000.0, 1)
    rec.info("release (to 37 % of the reduction) after a burst of 0.05 / 0.5 / 3 s [ms]: fixed 50 ms vs auto",
             {"fixed": [release_ms(b, False) for b in (0.05, 0.5, 3.0)], "auto": [release_ms(b, True) for b in (0.05, 0.5, 3.0)]})
    gx = np.concatenate([np.full(FS // 2, 0.1), np.zeros(FS)])
    _, mask = dynamics.gate(gx, FS, thresh_db=-30.0, range_db=-40.0, hold_ms=40.0, release_ms=120.0, f_sub1=100.0)
    close = int(np.flatnonzero(np.diff(mask.astype(int)) == -1)[0]) + 1
    gdb = dynamics.gate_gain_db(mask, FS, range_db=-40.0, attack_ms=2.0, release_ms=120.0, f_sub1=100.0)
    hold_meas = (close - FS // 2) / FS * 1000.0 - 15.0
    rel_meas = np.argmax(gdb[close:] <= -40.0 * (1 - np.exp(-1.0))) / FS * 1000.0
    rec.info("T-GATE-01: hold 40 ms / release 120 ms measured (after the 15 ms detector window) [ms]", (round(hold_meas, 2), round(rel_meas, 2)))
    rec.check("T-GATE-01: hold and release within 10 %", max(abs(hold_meas / 40.0 - 1), abs(rel_meas / 120.0 - 1)) * 100.0, "<=", 10.0, "%")

    rec.info("recommendation (R4, after the independent review D8)",
             "default detector Peak = hold of |x| over max(10 ms, 1.5 T_sub): steady state ripple-free (THD -124 dB at any "
             "attack/release) and the least onset splatter (-56 dB at 1 ms); RMS = mean square over the same window followed "
             "by a hold, with an attack floor of 0.25 T_sub (onset -42 dB); no ripple rule otherwise")

    # ---------------------------------------------------------------- plots
    fig, ax = plt.subplots(2, 3, figsize=(16, 8.5))
    for det, style in zip(DETECTORS, ("o-", "s-", "^-", "v-", "x-")):
        for rule, ls in ((False, ":"), (True, "-")):
            ax[0, 0].semilogx(ATTACKS, [thd[(det, rule, 50.0, a, 10.0)] for a in ATTACKS], style[0] + ls, label=f"{det}, rule {'on' if rule else 'off'}", ms=4)
    ax[0, 0].axhline(-60, color="gray", lw=0.6)
    ax[0, 0].set(xlabel="attack [ms] (release 10 ms)", ylabel="THD [dB]", ylim=(-130, -10), title="50 Hz, GR 6 dB: THD by detector")
    ax[0, 0].legend(fontsize=6, ncol=2); ax[0, 0].grid(alpha=.3, which="both")
    fo = sorted(off)
    for det in ("rms", "peak", "rms_window"):
        ax[0, 1].plot(fo, [off[f][det] for f in fo], "o-", label=det)
    ax[0, 1].axhline(-60, color="gray", lw=0.6)
    ax[0, 1].set(xlabel="tone [Hz] (f_sub1 = 40 Hz)", ylabel="THD [dB]", title="Off-frequency robustness")
    ax[0, 1].legend(fontsize=7); ax[0, 1].grid(alpha=.3)
    for la, mk in ((2.0, "o-"), (5.0, "s-")):
        ax[0, 2].semilogx([50, 100, 300, 1000], [lim[f"lookahead {la:g} ms, release {r:g} ms"] for r in (50.0, 100.0, 300.0, 1000.0)], mk, label=f"lookahead {la:g} ms")
    ax[0, 2].axhline(lim["live (c tanh(x/c), ADAA)"], color="r", ls="--", label="live tanh")
    ax[0, 2].set(xlabel="release [ms]", ylabel="THD [dB]", title="Limiter, 40 Hz sine 6 dB over the ceiling")
    ax[0, 2].legend(fontsize=7); ax[0, 2].grid(alpha=.3, which="both")
    xx = np.concatenate([signals.sine(100.0, FS, 0.5, level_db=-6.0), signals.sine(100.0, FS, 1.5, level_db=-40.0)])
    for auto in (False, True):
        _, g = dynamics.compressor(xx, FS, thresh_db=-30.0, ratio=4.0, knee_db=0.0, attack_ms=5.0, release_ms=50.0, detect="peak", f_sub1=100.0, auto_release=auto)
        ax[1, 0].plot(np.arange(len(g)) / FS, g, label="auto release" if auto else "release 50 ms")
    ax[1, 0].set(xlabel="s", ylabel="GR [dB]", title="Auto release after 0.5 s of reduction")
    ax[1, 0].legend(fontsize=7); ax[1, 0].grid(alpha=.3)
    t = np.arange(len(gx)) / FS
    ax[1, 1].plot(t * 1000, gdb, label="gate gain [dB]")
    ax[1, 1].plot(t * 1000, -40 * (~mask), ":", label="closed (x -40)")
    ax[1, 1].set(xlabel="ms", xlim=(400, 900), title="Gate: hold 40 ms, release 120 ms")
    ax[1, 1].legend(fontsize=7); ax[1, 1].grid(alpha=.3)
    yl = dynamics.limiter(xs, FS, ceiling_db=c_db, lookahead_ms=5.0, release_ms=100.0)
    tt = np.arange(len(xs)) / FS
    ax[1, 2].plot(tt[:int(0.1 * FS)] * 1000, xs[:int(0.1 * FS)], lw=0.8, label="input (+6 dB over)")
    ax[1, 2].plot(tt[:int(0.1 * FS)] * 1000, yl[:int(0.1 * FS)], lw=0.8, label="lookahead 5 ms, release 100 ms")
    ax[1, 2].axhline(c, color="gray", lw=0.6); ax[1, 2].axhline(-c, color="gray", lw=0.6)
    ax[1, 2].set(xlabel="ms", title="Limiter output (delayed by 5 ms)")
    ax[1, 2].legend(fontsize=7); ax[1, 2].grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(rec.fig_path("dynamics"), dpi=100)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
