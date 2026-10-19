# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-02: DIVIDE engine - accuracy, purity, continuity, polarity rule, gate behaviour, phase-halving comparison."""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from experiments._common import Recorder
from subgen_proto import divide, signals, svf, util


def _inband(f, fs, dur, snr_db, seed):
    s = signals.sine(f, fs, dur, -12.0)
    nz = signals.pink_noise(fs, dur, 30.0, 120.0, level_db=0.0, seed=seed)
    return s + nz * ((util.rms(s) / 10 ** (snr_db / 20.0)) / util.rms(nz))


def _db(v):
    return 20.0 * np.log10(max(v, 1e-300))


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
    rec.check("polarity rule: hits whose first sub lobe matches the source (of 100)", ok_sign, ">=", 100)
    rec.check("polarity rule: minimum correlation of sub waveform between hits", corr, ">=", 0.99)

    # 5. gate behaviour: kick (trend vs SPEC rule), clean-sine opening time
    kick = signals.kick(FS, n_hits=1, spacing=0.5, level_db=-6.0)
    gate = {}
    for mode in ("trend", "diff"):
        r = divide.divide(kick, FS, conf_mode=mode)
        m = divide.gate_mask(r.conf, 50.0)
        first = np.flatnonzero(m)
        gate[mode] = {"open_fraction_50-300ms": round(float(np.mean(m[int(.05 * FS):int(.30 * FS)])), 3),
                      "first_open_ms": round(1000 * first[0] / FS, 1) if first.size else None}
    rec.info("kick gate (SIG-03)", gate)
    rec.check("kick: gate open fraction 50-300 ms, 'trend' rule", gate["trend"]["open_fraction_50-300ms"], ">=", 0.9)
    rec.info("kick: SPEC rule ('diff') open fraction 50-300 ms", gate["diff"]["open_fraction_50-300ms"])
    opens = {}
    for f in (30.0, 55.0, 120.0):
        r = divide.divide(signals.sine(f, FS, 1.0, -12.0), FS)
        opens[f] = round(1000 * np.flatnonzero(divide.gate_mask(r.conf, 50.0))[0] / FS, 1)
    rec.info("clean sine: gate opens after (ms) [3 periods = 100.0, 54.5, 25.0]", opens)

    # 6. in-band noise sweep, chord and random noise
    sweep = {}
    for snr in (30, 20, 15, 10, 5, 3):
        fr, er = [], []
        for seed in (5, 6, 7, 8):
            r = divide.divide(_inband(60.0, FS, 4.0, snr, seed), FS)
            fr.append(np.mean(divide.gate_mask(r.conf, 50.0)[FS:]))
            er.append(abs(util.zc_freq(r.y1[FS:], FS) - 30.0) / 30.0)
        sweep[snr] = (round(float(np.mean(fr)), 3), round(100 * float(np.mean(er)), 3))
    rec.info("in-band noise: SNR -> (gate open fraction, mean |f/2 error| %)", sweep)
    rec.check("in-band SNR 20 dB: gate open fraction", sweep[20][0], ">=", 0.9)
    rec.check("in-band SNR 3 dB: gate open fraction", sweep[3][0], "<=", 0.2)
    fr_pink = []
    for seed in (4, 5, 6, 7, 8):
        r = divide.divide(signals.pink_noise(FS, 3.0, 30.0, 120.0, level_db=-12.0, seed=seed), FS)
        fr_pink.append(np.mean(divide.gate_mask(r.conf, 50.0)[int(0.1 * FS):]))
    rec.info("random 30-120 Hz noise: open fraction per seed", [round(float(v), 4) for v in fr_pink])
    rec.check("random noise: worst open fraction", max(fr_pink), "<=", 0.03)
    r = divide.divide(signals.chord(FS, (55.0, 69.0, 82.0), 3.0, -12.0), FS)
    rec.check("chord 55/69/82 Hz: gate open fraction", float(np.mean(divide.gate_mask(r.conf, 50.0)[int(0.1 * FS):])),
              "<=", 0.0)

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
