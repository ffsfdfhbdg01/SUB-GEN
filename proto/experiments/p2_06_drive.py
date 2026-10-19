# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-06: Drive / Saturate - aliasing by quality tier, harmonic spectra vs a 16x reference, Auto Gain, DC."""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from experiments._common import Recorder
from subgen_proto import drive, signals, util

FS = 48000
MODES = drive.MODES
CONFIGS = {"Live (ADAA)": dict(os_factor=1, adaa=True), "Mix (ADAA + 2x)": dict(os_factor=2, adaa=True),
           "Print (ADAA + 4x)": dict(os_factor=4, adaa=True), "naive 1x": dict(os_factor=1, adaa=False)}
TARGET = {"Live (ADAA)": -80.0, "Mix (ADAA + 2x)": -90.0, "Print (ADAA + 4x)": -90.0}
TONES = (31.0, 81.0, 121.0)  # near 30 / 80 / 120 Hz, but sharing no factor with 44.1 / 48 kHz: aliases are visible
KW = dict(tone_hz=None, auto_gain=False)


def tone(f, fs, level=-6.0, dur=1.5):
    return signals.sine(f, fs, dur, level_db=level)


def lines(y, fs, f0, n):
    """Amplitudes of harmonics 1..n of f0 in a whole-second segment (bin = Hz)."""
    return np.array([2.0 * np.abs(np.fft.rfft(y)[int(round(k * f0))]) / len(y) for k in range(1, n + 1)])


def main():
    rec = Recorder("p2_06")

    # ---------------------------------------------------------------- 1. aliasing by tier (T-ALIAS-01)
    table = {}
    for fs in (44100, 48000):
        for f in TONES:
            x = tone(f, fs)
            for mode in ("hard", "fold"):
                for d in (25.0, 50.0, 100.0):
                    table[(fs, f, mode, d)] = {c: drive.nonharmonic_db(x, fs, f, drive_pct=d, mode=mode, **p, **KW)
                                               for c, p in CONFIGS.items()}
    summary = {}
    for c in CONFIGS:
        for mode in ("hard", "fold"):
            vals = [v[c] for k, v in table.items() if k[2] == mode]
            summary[f"{c} | {mode}"] = {"worst": round(max(vals), 1), "best": round(min(vals), 1)}
    rec.info("aliasing (non-harmonic power re total, -6 dBFS tones 31/81/121 Hz, fs 44.1 + 48 kHz, drive 25/50/100 %): worst / best", summary)
    rec.info("aliasing worst case per config and cell", {f"{c}": {f"{k[0]}/{k[1]:g}Hz/{k[2]}/{k[3]:g}%": round(v[c], 1)
                                                                      for k, v in sorted(table.items(), key=lambda kv: -kv[1][c])[:3]} for c in TARGET})
    for c, thr in TARGET.items():
        worst = max(v[c] for v in table.values())
        passed = sum(1 for v in table.values() if v[c] <= thr)
        rec.info(f"T-ALIAS-01 {c}: target {thr} dB -> cells meeting it / total, worst", (passed, len(table), round(worst, 1)))
    per_drive = {c: {d: round(max(v[c] for k, v in table.items() if k[2] == "hard" and k[3] == d), 1) for d in (25.0, 50.0, 100.0)}
                 for c in CONFIGS}
    rec.info("Hard: worst aliasing by drive % (3 tones x 2 sample rates) [dB re total]", per_drive)
    rec.info("Fold: worst aliasing over all cells [dB] (FM-like output, bandwidth ~ (beta + 1) f0: no aliasing at these tones)",
             {c: round(max(v[c] for k, v in table.items() if k[2] == "fold"), 1) for c in CONFIGS})
    # The SPEC targets as written (T-ALIAS-01 [P2]): Live -80, Mix -90, Print -90. Hard at high drive misses the first two.
    for c, thr in TARGET.items():
        rec.check(f"T-ALIAS-01 SPEC target, {c}: worst over all cells", max(v[c] for v in table.values()), "<=", thr, "dB")
    # in-band view: only what lands below a 500 Hz sub low-pass survives the rest of the chain
    sub = {}
    for c, p in CONFIGS.items():
        sub[c] = round(max(drive.nonharmonic_db(tone(f, FS), FS, f, drive_pct=100.0, mode=m, f_hi=500.0, **p, **KW)
                           for f in TONES for m in ("hard", "fold")), 1)
    rec.info("aliasing that lands below 500 Hz (sub band), worst over tones/hard+fold at drive 100 %, 48 kHz [dB re total]", sub)

    # ---------------------------------------------------------------- 2. metrics compared (Hard, 100 %, 81 Hz)
    x = tone(81.0, FS, level=-12.0)
    m = int(0.25 * FS)
    refA = util.frac_delay(drive.process(x, FS, drive_pct=100.0, mode="hard", os_factor=16, adaa=True, **KW)[m:-m], -0.5 / 16)
    spr = np.fft.rfft(refA) / len(refA)
    freqs = np.fft.rfftfreq(len(refA), 1.0 / FS)
    keep = (freqs >= 20) & (freqs <= 0.45 * FS)
    harm = np.zeros(len(freqs), bool)
    harm[np.round(np.arange(1, int(0.5 * FS / 81.0)) * 81.0).astype(int)] = True
    metric_tab = {}
    for name, p in CONFIGS.items():
        y = util.frac_delay(drive.process(x, FS, drive_pct=100.0, mode="hard", **p, **KW)[m:-m], -(0.5 / p["os_factor"] if p["adaa"] else 0.0))
        sp = np.fft.rfft(y) / len(y)
        tot = np.sqrt(np.sum(np.abs(spr[keep]) ** 2))
        metric_tab[name] = {
            "error vs ADAA-16x reference": round(20 * np.log10(np.sqrt(np.sum(np.abs(sp[keep] - spr[keep]) ** 2)) / tot), 1),
            "non-harmonic (aliasing)": round(20 * np.log10(np.sqrt(np.sum(np.abs(sp[keep & ~harm]) ** 2)) / tot), 1),
            "harmonic-line error (droop)": round(20 * np.log10(max(np.sqrt(np.sum(np.abs(sp[keep & harm] - spr[keep & harm]) ** 2)), 1e-300) / tot), 1)}
    rec.info("Hard 100 %, 81 Hz, 48 kHz: error vs reference | non-harmonic | harmonic-line error [dB re total]", metric_tab)
    blind = drive.nonharmonic_db(tone(80.0, FS), FS, 80.0, drive_pct=100.0, mode="hard", **CONFIGS["naive 1x"], **KW)
    rec.info("T-ALIAS-01 with an 80 Hz tone at 48 kHz (fs / f0 = 600) and a NAIVE shaper: non-harmonic power [dB] "
             "(-300 = nothing visible: every alias lands on a harmonic line)", round(blind, 1))
    rec.check("an 80 Hz tone at 48 kHz cannot reveal aliasing (every alias lands on a harmonic)", blind, "<=", -200.0, "dB")

    # ---------------------------------------------------------------- 3. harmonic spectra vs the 16x reference (T-DRV-01)
    f0, nlin = 55.0, 60
    xr = signals.sine(f0, FS, 3.0, level_db=-12.0)
    spec_err, spectra = {}, {}
    for mode in MODES:
        for d in (25.0, 50.0, 100.0):
            sel = slice(FS, 2 * FS)
            ref = drive.process(xr, FS, drive_pct=d, mode=mode, os_factor=16, adaa=True, **KW)[sel]
            a_ref = lines(ref, FS, f0, nlin)
            spectra[(mode, d)] = a_ref
            big = 20 * np.log10(a_ref / a_ref.max()) >= -60.0
            for c, p in CONFIGS.items():
                a = lines(drive.process(xr, FS, drive_pct=d, mode=mode, **p, **KW)[sel], FS, f0, nlin)
                spec_err[(mode, d, c)] = float(np.max(np.abs(20 * np.log10(a[big] / a_ref[big]))))
    for c in CONFIGS:
        rec.info(f"T-DRV-01 harmonic level error vs 16x reference (harmonics >= -60 dB), worst over modes/drives: {c}",
                 round(max(v for k, v in spec_err.items() if k[2] == c), 2))
    rec.check("T-DRV-01: harmonics >= -60 dB within 1 dB of the 16x reference (Print: ADAA + 4x)",
              max(v for k, v in spec_err.items() if k[2] == "Print (ADAA + 4x)"), "<=", 1.0, "dB")
    rec.check("T-DRV-01: harmonics >= -60 dB within 1 dB of the 16x reference (Mix: ADAA + 2x)",
              max(v for k, v in spec_err.items() if k[2] == "Mix (ADAA + 2x)"), "<=", 1.0, "dB")
    rec.info("T-DRV-01 per character at drive 100 %: harmonics 1..6 [dB re fundamental] (reference, 55 Hz, -12 dBFS)",
             {m_: [round(20 * np.log10(spectra[(m_, 100.0)][k] / spectra[(m_, 100.0)][0]), 1) for k in range(6)] for m_ in MODES})

    # ---------------------------------------------------------------- 4. Auto Gain (T-DRV-03)
    drives = (5.0, 10.0, 25.0, 50.0, 75.0, 100.0)
    ag = {m_: {b: [round(drive.auto_gain_db(m_, d, b), 2) for d in drives] for b in (-100.0, 0.0, 100.0)} for m_ in MODES}
    rec.info("Auto Gain G_c [dB] for drive 5/10/25/50/75/100 %, by mode and balance -100/0/+100", ag)
    dev, dev_tone = [], []
    xs = signals.sine(55.0, FS, 3.0, level_db=-12.0)
    ref_db = 20 * np.log10(util.rms(xs[FS:]))
    for m_ in MODES:
        for d in drives:
            for b in (-100.0, 0.0, 100.0):
                y = drive.dc_block(drive.process(xs, FS, drive_pct=d, mode=m_, balance=b, tone_hz=None), FS)
                dev.append(abs(20 * np.log10(util.rms(y[FS:])) - ref_db))
                y = drive.dc_block(drive.process(xs, FS, drive_pct=d, mode=m_, balance=b), FS)  # Tone 500 Hz
                dev_tone.append(20 * np.log10(util.rms(y[FS:])) - ref_db)
    rec.check("T-DRV-03: RMS of the reference sine vs drive 0 (all modes, drives 5-100 %, balance -100/0/+100; no Tone)", max(dev), "<=", 0.5, "dB")
    rec.info("with the default Tone filter (500 Hz) the level changes by [dB]: worst / best (Fold and Hard spread above 500 Hz)",
             (round(min(dev_tone), 2), round(max(dev_tone), 2)))
    fs_dev = {}
    for fs in (44100, 96000):
        xf = signals.sine(55.0, fs, 3.0, level_db=-12.0)
        fs_dev[fs] = round(max(abs(20 * np.log10(util.rms(drive.dc_block(drive.process(xf, fs, drive_pct=d, mode=m_, tone_hz=None), fs)[fs:]) / util.rms(xf[fs:])))
                               for m_ in MODES for d in (10.0, 50.0, 100.0)), 3)
    rec.info("Auto Gain RMS deviation at other sample rates [dB]", fs_dev)

    # ---------------------------------------------------------------- 5. DC (T-DRV-04)
    dcs = {}
    for m_, b in (("tube", 0.0), ("tube", -100.0), ("tube", 100.0), ("soft", -100.0), ("hard", -100.0), ("fold", -100.0)):
        y = drive.process(xs, FS, drive_pct=100.0, mode=m_, balance=b, tone_hz=None)
        dcs[f"{m_}/{b:+.0f}"] = (round(float(np.mean(y[FS:])), 4), float(np.mean(drive.dc_block(y, FS)[FS:])))
    rec.info("DC of the Drive output before / after the 5 Hz high-pass (drive 100 %, reference sine)", dcs)
    rec.check("T-DRV-04: DC after the 5 Hz high-pass (worst)", 20 * np.log10(max(abs(v[1]) for v in dcs.values()) + 1e-300), "<=", -80.0, "dBFS")

    # ---------------------------------------------------------------- 6. identity and filters
    rec.check("T-DRV-05: drive = 0 is bit-exact", float(not np.array_equal(drive.process(xs, FS, drive_pct=0.0, mode="fold"), xs)), "==", 0.0)

    # ---------------------------------------------------------------- plots
    fig, ax = plt.subplots(2, 3, figsize=(16, 8.5))
    u = np.linspace(-3, 3, 600)
    for m_ in MODES:
        f, _ = drive.shape_fn(m_, drive.effective_bias(m_, 0.0))
        ax[0, 0].plot(u, f(u), label=m_)
    ax[0, 0].set(xlabel="shaper input", ylabel="output", title="Characters (balance 0)")
    ax[0, 0].legend(fontsize=7); ax[0, 0].grid(alpha=.3)
    k = np.arange(1, 21)
    for m_ in MODES:
        a = spectra[(m_, 100.0)][:20]
        ax[0, 1].plot(k, 20 * np.log10(np.maximum(a / a.max(), 1e-6)), "o-", ms=3, label=m_)
    ax[0, 1].set(xlabel="harmonic of 55 Hz", ylabel="dB re strongest line", ylim=(-80, 3), title="Harmonics at drive 100 % (-12 dBFS sine)")
    ax[0, 1].legend(fontsize=7); ax[0, 1].grid(alpha=.3)
    for c in CONFIGS:
        pts = {d: max(v[c] for kk, v in table.items() if kk[2] == "hard" and kk[3] == d) for d in (25.0, 50.0, 100.0)}
        ax[0, 2].plot(list(pts), list(pts.values()), "o-", label=c)
    ax[0, 2].axhline(-80, color="gray", lw=0.6); ax[0, 2].axhline(-90, color="gray", lw=0.6, ls="--")
    ax[0, 2].set(xlabel="drive %", ylabel="non-harmonic power [dB]", title="Hard: aliasing, worst of 3 tones x 2 sample rates")
    ax[0, 2].legend(fontsize=7); ax[0, 2].grid(alpha=.3)
    for c in CONFIGS:
        pts = {d: max(v[c] for kk, v in table.items() if kk[2] == "fold" and kk[3] == d) for d in (25.0, 50.0, 100.0)}
        ax[1, 0].plot(list(pts), list(pts.values()), "o-", label=c)
    ax[1, 0].axhline(-80, color="gray", lw=0.6); ax[1, 0].axhline(-90, color="gray", lw=0.6, ls="--")
    ax[1, 0].set(xlabel="drive %", ylabel="non-harmonic power [dB]", title="Fold: aliasing, worst of 3 tones x 2 sample rates")
    ax[1, 0].legend(fontsize=7); ax[1, 0].grid(alpha=.3)
    for m_ in MODES:
        ax[1, 1].plot(drives, ag[m_][0.0], "o-", label=f"{m_}")
    ax[1, 1].set(xlabel="drive %", ylabel="Auto Gain [dB]", title="Auto Gain compensation (balance 0)")
    ax[1, 1].legend(fontsize=7); ax[1, 1].grid(alpha=.3)
    xx = tone(81.0, FS, level=-12.0)
    for name in ("naive 1x", "Live (ADAA)", "Mix (ADAA + 2x)", "Print (ADAA + 4x)"):
        y = drive.process(xx, FS, drive_pct=100.0, mode="hard", **CONFIGS[name], **KW)[m:-m]
        sp = np.abs(np.fft.rfft(y)) / len(y) * 2
        fq = np.fft.rfftfreq(len(y), 1.0 / FS)
        hh = np.zeros(len(fq), bool); hh[np.round(np.arange(1, int(0.5 * FS / 81.0) + 1) * 81.0).astype(int)] = True
        ax[1, 2].plot(fq[~hh][::40] / 1000, 20 * np.log10(np.maximum(sp[~hh][::40], 1e-12)), ".", ms=1.5, label=name)
    ax[1, 2].set(xlabel="kHz", ylabel="dB", ylim=(-130, -40), xlim=(0, 21), title="Non-harmonic lines (aliasing), Hard 100 %, 81 Hz")
    ax[1, 2].legend(fontsize=7, markerscale=6); ax[1, 2].grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(rec.fig_path("drive"), dpi=100)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
