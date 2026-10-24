# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-02: DIVIDE engine - accuracy, purity, continuity, polarity rule, gate behaviour, phase-halving comparison.

Re-run after the independent review (D8): note boundaries and polarity resets (R2), provisional confidence and the
gate lookahead (R1), shape as a cutoff (R5) and conf_gate edges (I1) are measured on the default output."""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from experiments._common import Recorder
from subgen_proto import band, divide, render, signals, svf, util


def _inband(f, fs, dur, snr_db, seed):
    s = signals.sine(f, fs, dur, -12.0)
    nz = signals.pink_noise(fs, dur, 30.0, 120.0, level_db=0.0, seed=seed)
    return s + nz * ((util.rms(s) / 10 ** (snr_db / 20.0)) / util.rms(nz))


def _db(v):
    return 20.0 * np.log10(max(v, 1e-300))


def _first_sign(seg, frac=0.1):
    j = int(np.argmax(np.abs(seg) > frac * np.max(np.abs(seg))))
    return np.sign(seg[j])


def _floor(fs, n, rms_db, seed=5):
    nz = signals.pink_noise(fs, n / fs, 30.0, 120.0, level_db=0.0, seed=seed)[:n]
    return nz * (10.0 ** (rms_db / 20.0) / np.sqrt(np.mean(nz ** 2)))


def main():
    rec = Recorder("p2_02")
    FS = 48000

    # 1. frequency accuracy of Sub -1 / Sub -2
    worst = 0.0
    table = {}
    for fs in (44100, 48000, 96000):
        for f in (30.0, 40.0, 55.0, 80.0, 120.0, 200.0):
            x = signals.sine(f, fs, 3.0, -18.0)
            res = divide.divide(x, fs)
            s = int(8.5 * fs / f)
            e1 = abs(util.zc_freq(res.y1[s:], fs) - f / 2) / (f / 2)
            e2 = abs(util.zc_freq(res.y2[s:], fs) - f / 4) / (f / 4)
            table[f"{fs}/{f:g}"] = (round(100 * e1, 6), round(100 * e2, 6))
            worst = max(worst, e1, e2)
    rec.info("frequency error [%] (Sub-1, Sub-2) per fs/f", table)
    rec.check("Sub -1 / Sub -2 worst frequency error (6 f x 3 fs)", 100 * worst, "<=", 0.1, "%")

    # 2. purity: 3f/2 content of Sub -1 (native / tracking low-pass / phase halving)
    pur = {}
    for f in (40.0, 55.0, 80.0, 120.0):
        x = signals.sine(f, FS, 4.0, -12.0)
        res = divide.divide(x, FS)
        seg = slice(FS, 3 * FS)
        row = {}
        for name, y in (("native", res.y1), ("smooth", divide.smooth_sub(res, FS, 1)),
                        ("phase_halving", divide.phase_halving(x, FS))):
            a = util.harmonic_amps(y[seg], FS, f / 2, n=5)  # k=1: f/2, k=3: 3f/2, k=5: 5f/2
            row[name] = {"3f/2_dB": round(_db(a[2] / a[0]), 1), "5f/2_dB": round(_db(a[4] / a[0]), 1),
                         "fund_rel_input_dB": round(_db(a[0] / 10 ** (-12 / 20)), 2)}
        pur[f] = row
    rec.info("Sub -1 purity: components relative to f/2", pur)
    rec.check("smooth Sub -1: 3f/2 component (worst over f)", max(r["smooth"]["3f/2_dB"] for r in pur.values()), "<=",
              -18.0, "dB")
    rec.check("phase halving: 3f/2 component (worst over f)", max(r["phase_halving"]["3f/2_dB"] for r in pur.values()),
              "<=", -40.0, "dB")
    rec.info("native shape: 3f/2 relative to f/2 (hand estimate -4.4 dB)", [r["native"]["3f/2_dB"] for r in pur.values()])

    # 3. continuity
    for name, x in (("sine 55 Hz", signals.sine(55.0, FS, 2.0, -6.0)),
                    ("kick", signals.kick(FS, n_hits=3, spacing=0.5)),
                    ("808", signals.bass_808(FS))):
        res = divide.divide(x, FS)
        ratio = max(np.max(np.abs(np.diff(res.y1))), np.max(np.abs(np.diff(res.y2)))) / np.max(np.abs(np.diff(x)))
        rec.check(f"continuity: max|dy| / max|dx| ({name})", ratio, "<=", 1.0 + 1e-12)

    # 4. polarity rule on the plan's 100 kicks
    n_hits = 100
    x = signals.kick(FS, n_hits=n_hits, spacing=0.5, levels_db=list(np.linspace(-24.0, -3.0, n_hits)))
    res = divide.divide(x, FS)
    hit = int(0.5 * FS)
    ok_sign, shapes = 0, []
    for i in range(n_hits):
        xs, ys = x[i * hit:(i + 1) * hit], res.y1[i * hit:(i + 1) * hit]
        pk = np.max(np.abs(xs))
        j = int(np.argmax(np.abs(xs) > 0.1 * pk))
        ok_sign += int(np.sign(ys[j]) == np.sign(xs[j]))
        shapes.append(ys[: int(0.2 * FS)] / pk)
    corr = min(float(np.dot(shapes[0], s) / np.sqrt(np.dot(shapes[0], shapes[0]) * np.dot(s, s))) for s in shapes[1:])
    rec.check("polarity rule: hits whose first sub lobe matches the source (of 100, native y1)", ok_sign, ">=", 100)
    rec.check("polarity rule: minimum correlation of sub waveform between hits (native y1)", corr, ">=", 0.99)

    # 4b. polarity on the default output (smoothed + gated), separated hits over noise floors and hum, and
    #     overlapping tails (D8 C2 -> R2). 20 hits each.
    n20, hum = 20, lambda n: 10.0 ** (-70.0 / 20.0) * np.sin(2.0 * np.pi * 31.0 * np.arange(n) / FS)
    def hits(spacing=0.5, t60=0.3, extra=None):
        xx = signals.kick(FS, n_hits=n20, spacing=spacing, level_db=-6.0, t60=t60)
        return band.range_filter(xx + (extra(len(xx)) if extra else 0.0), FS)
    variants = {"silence": hits(), "hum 31 Hz -70 dBFS": hits(extra=hum)}
    variants.update({f"pink {lv:g} dBFS RMS": hits(extra=lambda n, lv=lv: _floor(FS, n, lv)) for lv in (-90, -80, -70, -60)})
    pol = {}
    for quality in ("mix", "live"):
        for label, xb in variants.items():
            y = render.divide_sub(xb, FS, quality=quality)
            h = int(0.5 * FS)
            ok = sum(int(_first_sign(y[i * h:(i + 1) * h]) == _first_sign(xb[i * h:(i + 1) * h])) for i in range(n20))
            sh = [y[i * h:i * h + int(0.2 * FS)] for i in range(n20)]
            cmin = min(float(np.dot(sh[0], v) / np.sqrt(np.dot(sh[0], sh[0]) * np.dot(v, v))) for v in sh[1:])
            pol[f"{quality} | {label}"] = (ok, round(cmin, 4))
    rec.info("default output, separated hits: (first lobe matches the source, of 20; min correlation)", pol)
    rec.check("default output, separated hits: worst first-lobe agreement (Mix and Live, 6 variants)",
              min(v[0] for v in pol.values()), ">=", n20)
    rec.check("default output, separated hits: worst correlation between hits", min(v[1] for v in pol.values()), ">=", 0.99)
    tails = {}
    for spacing in (0.45, 0.5):
        xb = hits(spacing=spacing, t60=1.0)
        r = divide.divide(xb, FS)
        y = render.divide_sub(xb, FS, quality="mix")
        h = int(spacing * FS)
        native = sum(int(_first_sign(r.y1[i * h:(i + 1) * h]) == _first_sign(xb[i * h:(i + 1) * h])) for i in range(n20))
        out_signs = {float(_first_sign(y[i * h:(i + 1) * h])) for i in range(1, n20)}
        tails[f"T60 1 s every {spacing:g} s"] = {"native y1 matches (of 20)": native, "output signs, hits 1-19": sorted(out_signs)}
    rec.info("overlapping tails (each hit over the previous one's tail, 13.5 dB down at 0.45 s), Mix", tails)
    rec.check("overlapping tails: native y1 first lobe matches the source (worst of 2 spacings, of 20)",
              min(v["native y1 matches (of 20)"] for v in tails.values()), ">=", n20)
    rec.check("overlapping tails: the output looks alike from hit to hit (distinct first-lobe signs, worst)",
              max(len(v["output signs, hits 1-19"]) for v in tails.values()), "<=", 1)

    # 5. gate behaviour: kick (trend vs SPEC rule), clean-sine opening time
    kick = signals.kick(FS, n_hits=1, spacing=0.5, level_db=-6.0)
    gate = {}
    for mode in ("trend", "diff"):
        r = divide.divide(kick, FS, conf_mode=mode)
        m = divide.divide_gate_mask(r, 50.0)
        first = np.flatnonzero(m)
        gate[mode] = {"open_fraction_50-300ms": round(float(np.mean(m[int(.05 * FS):int(.30 * FS)])), 3),
                      "first_open_ms": round(1000 * first[0] / FS, 1) if first.size else None}
    rec.info("kick gate (SIG-03; conf provisional 1 from the note start)", gate)
    rec.check("kick: gate open fraction 50-300 ms, 'trend' rule", gate["trend"]["open_fraction_50-300ms"], ">=", 0.9)
    rec.info("kick: SPEC 0.1 rule ('diff') open fraction 50-300 ms", gate["diff"]["open_fraction_50-300ms"])
    xb1 = band.range_filter(kick, FS)
    n40 = int(0.040 * FS)
    onset = {}
    for quality, look in (("live", 0.0), ("mix", 0.045), ("print", 0.055)):
        gated = render.divide_sub(xb1, FS, quality=quality)
        ungated = divide.smooth_sub(divide.divide(xb1, FS, live=quality == "live", lookahead_s=look), FS, 1)
        onset[quality] = round(float(np.sum(gated[:n40] ** 2) / np.sum(ungated[:n40] ** 2)), 3)
    rec.info("kick through the range filter: gated / ungated sub energy in the first 40 ms (D8 C1: was 0.005)", onset)
    rec.check("kick onset: gated / ungated sub energy in the first 40 ms (worst quality)", min(onset.values()), ">=", 0.5)
    opens = {}
    for f in (30.0, 55.0, 120.0):
        r = divide.divide(np.concatenate([np.zeros(FS // 5), signals.sine(f, FS, 1.0, -12.0)]), FS)
        opens[f] = round(1000 * (np.flatnonzero(divide.divide_gate_mask(r, 50.0))[0] - FS // 5) / FS, 1)
    rec.info("clean sine after silence: gate opens after the note start (ms; provisional, R1)", opens)

    # 6. in-band noise sweep, chord and random noise
    sweep = {}
    for snr in (30, 20, 15, 10, 5, 3):
        fr, er = [], []
        for seed in (5, 6, 7, 8):
            r = divide.divide(_inband(60.0, FS, 4.0, snr, seed), FS)
            fr.append(np.mean(divide.divide_gate_mask(r, 50.0)[FS:]))
            er.append(abs(util.zc_freq(r.y1[FS:], FS) - 30.0) / 30.0)
        sweep[snr] = (round(float(np.mean(fr)), 3), round(100 * float(np.mean(er)), 3))
    rec.info("in-band noise: SNR -> (gate open fraction, mean |f/2 error| %)", sweep)
    rec.check("in-band SNR 20 dB: gate open fraction", sweep[20][0], ">=", 0.9)
    rec.check("in-band SNR 3 dB: gate open fraction", sweep[3][0], "<=", 0.2)
    fr_pink = []
    for seed in (4, 5, 6, 7, 8):
        r = divide.divide(signals.pink_noise(FS, 3.0, 30.0, 120.0, level_db=-12.0, seed=seed), FS)
        fr_pink.append(np.mean(divide.divide_gate_mask(r, 50.0)[int(0.1 * FS):]))
    rec.info("random 30-120 Hz noise: open fraction per seed", [round(float(v), 4) for v in fr_pink])
    rec.check("random noise: worst open fraction", max(fr_pink), "<=", 0.03)
    r = divide.divide(signals.chord(FS, (55.0, 69.0, 82.0), 3.0, -12.0), FS)
    rec.check("chord 55/69/82 Hz: gate open fraction", float(np.mean(divide.divide_gate_mask(r, 50.0)[int(0.1 * FS):])),
              "<=", 0.0)
    chord = signals.chord(FS, (55.0, 69.0, 82.0), 3.0, -12.0)
    lev = {q: round(_db(util.rms(render.divide_sub(chord, FS, quality=q)[int(0.1 * FS):]) / util.rms(chord[int(0.1 * FS):])), 1)
           for q in ("live", "mix")}
    rec.info("chord from silence: gated sub level after 100 ms re the source [dB]", lev)
    rec.check("T-DIV-05 chord: sub level after 100 ms (worst of Live, Mix)", max(lev.values()), "<=", -40.0, "dB")
    chord_b = band.range_filter(chord, FS)  # F-1 (Phase 3): the chord as DIVIDE sees it, through the source band
    lev_b = {q: round(_db(util.rms(render.divide_sub(chord_b, FS, quality=q)[int(0.1 * FS):]) / util.rms(chord[int(0.1 * FS):])), 1)
             for q in ("live", "mix")}
    rec.info("chord through the source band (F-1: was -10.3 dB with one confirmation): sub level after 100 ms [dB]", lev_b)
    rec.check("T-DIV-05 chord through the source band: sub level after 100 ms (worst of Live, Mix)", max(lev_b.values()),
              "<=", -40.0, "dB")

    # 6b. conf_gate edges (D8 I1): a 55 Hz note (1 s), then random 30-120 Hz noise (2 s): open fraction in the noise
    x = np.concatenate([signals.sine(55.0, FS, 1.0, -12.0), signals.pink_noise(FS, 2.0, 30.0, 120.0, level_db=-12.0, seed=6)])
    r = divide.divide(x, FS)
    edges = {g: round(float(np.mean(divide.divide_gate_mask(r, g)[int(1.1 * FS):])), 3) for g in (0.0, 1.0, 50.0, 100.0)}
    rec.info("conf_gate -> gate open fraction during the noise after a note (was 1.0 at conf_gate 0)", edges)
    rec.check("conf_gate 0: the gate closes in noise (open fraction)", edges[0.0], "<=", 0.5)

    # 6c. shape (R5): level of the f/2 component vs shape, re shape 0
    res55 = divide.divide(signals.sine(55.0, FS, 4.0, -12.0), FS)
    ref = util.harmonic_amps(divide.smooth_sub(res55, FS, 1)[FS:3 * FS], FS, 27.5, n=1)[0]
    shape_tab = {sh: round(_db(util.harmonic_amps(divide.smooth_sub(res55, FS, 1, shape_pct=sh)[FS:3 * FS], FS, 27.5, n=1)[0] / ref), 2)
                 for sh in (25.0, 50.0, 75.0, 100.0)}
    rec.info("shape -> f/2 level re shape 0 [dB] (mixing the paths gave -2.1 / -3.1 / -2.1 dB at 25 / 50 / 75 %)", shape_tab)
    rec.check("shape: worst f/2 level change", max(abs(v) for v in shape_tab.values()), "<=", 0.5, "dB")

    # 7. phase halving vs divider on noisy sine (frequency error of the sub)
    cmp_ = {}
    for snr in (30, 20, 10):
        x = _inband(55.0, FS, 4.0, snr, 5)
        r = divide.divide(x, FS)
        yd = divide.smooth_sub(r, FS, 1)
        yp = divide.phase_halving(x, FS)
        cmp_[snr] = {"divider_smooth_f_err_%": round(100 * abs(util.zc_freq(yd[FS:3 * FS], FS) - 27.5) / 27.5, 3),
                     "phase_halving_f_err_%": round(100 * abs(util.zc_freq(yp[FS:3 * FS], FS) - 27.5) / 27.5, 3)}
    rec.info("noisy sine 55 Hz: Sub -1 frequency error, divider+smooth vs phase halving", cmp_)

    # --- plots
    t = np.arange(int(0.2 * FS)) / FS
    x = signals.sine(55.0, FS, 1.0, -12.0)
    res = divide.divide(x, FS)
    s0 = int(0.4 * FS)
    fig, ax = plt.subplots(2, 2, figsize=(12, 7))
    ax[0, 0].plot(t * 1000, x[s0:s0 + len(t)], label="x (55 Hz)")
    ax[0, 0].plot(t * 1000, res.y1[s0:s0 + len(t)], label="y1 native (27.5 Hz)")
    ax[0, 0].plot(t * 1000, divide.smooth_sub(res, FS, 1)[s0:s0 + len(t)], label="y1 smooth")
    ax[0, 0].plot(t * 1000, res.y2[s0:s0 + len(t)], label="y2 native (13.75 Hz)", alpha=0.7)
    ax[0, 0].set(xlabel="ms", title="DIVIDE waveforms"); ax[0, 0].legend(fontsize=7); ax[0, 0].grid(alpha=.3)
    for mode, ls in (("trend", "-"), ("diff", "--")):
        r = divide.divide(kick, FS, conf_mode=mode)
        ax[0, 1].plot(np.arange(len(kick)) / FS * 1000, r.conf, ls, label=f"conf, rule '{mode}'")
    ax[0, 1].plot(np.arange(len(kick)) / FS * 1000, np.abs(kick), color="gray", alpha=0.4, label="|kick|")
    ax[0, 1].axhline(0.5, color="k", lw=0.5)
    ax[0, 1].set(xlabel="ms", xlim=(0, 400), title="Kick 150->45 Hz: confidence (SPEC rule vs trend)")
    ax[0, 1].legend(fontsize=7); ax[0, 1].grid(alpha=.3)
    snrs = sorted(sweep)
    ax[1, 0].plot(snrs, [sweep[s][0] for s in snrs], "o-")
    ax[1, 0].set(xlabel="in-band SNR [dB]", ylabel="gate open fraction", title="Gate vs in-band noise (mean of 4 seeds)")
    ax[1, 0].grid(alpha=.3)
    x4 = signals.sine(55.0, FS, 4.0, -12.0)
    r4 = divide.divide(x4, FS)
    for name, y in (("native", r4.y1), ("smooth", divide.smooth_sub(r4, FS, 1)), ("phase halving", divide.phase_halving(x4, FS))):
        seg = y[FS:3 * FS] * np.hanning(2 * FS)
        spec = 20 * np.log10(np.abs(np.fft.rfft(seg)) / (FS) + 1e-12)
        fr = np.fft.rfftfreq(2 * FS, 1 / FS)
        ax[1, 1].semilogx(fr[1:], spec[1:], label=name, lw=0.8)
    ax[1, 1].set(xlim=(5, 400), ylim=(-140, -20), xlabel="Hz", ylabel="dB", title="Spectrum of Sub -1 (55 Hz input)")
    ax[1, 1].legend(fontsize=7); ax[1, 1].grid(alpha=.3, which="both")
    fig.tight_layout()
    fig.savefig(rec.fig_path("divide"), dpi=110)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
