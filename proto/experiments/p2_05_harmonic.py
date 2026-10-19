# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-05: HARMONIC engine - weights vs amplitude and frequency, envelope estimator, DC, balance, IMD, 808 onset."""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from experiments._common import Recorder
from subgen_proto import band, envelope, harmonic, signals, svf, util

FS = 48000


def db(v):
    return 20.0 * np.log10(max(v, 1e-300))


def literal(x, fs, amount_pct=100.0, balance=0.0, k_max=5):
    """SPEC §6.5 as written: e_pk_s = plain peak follower (attack 5 ms, release 60 ms)."""
    e = envelope.peak_follower(x, fs, 5.0, 60.0)
    xn = np.clip(x / np.maximum(e, 1e-4), -1.0, 1.0)
    return svf.process_fast(svf.design_butter(2, 15.0, fs, "high"), e * harmonic.chebyshev_sum(xn, amount_pct, balance, k_max))


def weight_errors(y, f0, amp, fs=FS, k_max=5):
    """dB error of harmonics 2..k_max against amplitude amp / k over a whole number of cycles."""
    n_cyc = int(np.floor(1.0 * f0))  # cycles in ~1 s
    seg = y[int(1.0 * fs):int(1.0 * fs) + int(round(n_cyc * fs / f0))]
    a = util.harmonic_amps(seg, fs, f0, k_max)
    return [db(a[k - 1] / amp) + db(k) for k in range(2, k_max + 1)], a


def main():
    rec = Recorder("p2_05")
    freqs = (30.0, 40.0, 55.0, 80.0, 120.0)
    levels = (-40.0, -24.0, -12.0, -6.0, -1.0)

    # ---------------------------------------------------------------- 1. weights vs frequency and level
    err_new, err_lit = {}, {}
    for f in freqs:
        for lv in levels:
            x = signals.sine(f, FS, 3.0, level_db=lv)
            amp = 10.0 ** (lv / 20.0)
            err_new[(f, lv)] = max(abs(e) for e in weight_errors(harmonic.harmonic(x, FS, amount_pct=100.0, k_max=5), f, amp)[0])
            err_lit[(f, lv)] = max(abs(e) for e in weight_errors(literal(x, FS), f, amp)[0])
    rec.info("weights error, max over k = 2..5 [dB]: max-hold envelope, by frequency (all levels)",
             {f: round(max(err_new[(f, lv)] for lv in levels), 3) for f in freqs})
    rec.info("weights error, max over k = 2..5 [dB]: SPEC-literal 5/60 ms peak follower, by frequency (all levels)",
             {f: round(max(err_lit[(f, lv)] for lv in levels), 2) for f in freqs})
    rec.check("T-HARM-01: weights vs w_k, worst over 5 f x 5 levels (max-hold envelope)", max(err_new.values()), "<=", 0.5, "dB")
    rec.info("SPEC-literal envelope: worst weights error (would fail T-HARM-01)", round(max(err_lit.values()), 2))

    # ---------------------------------------------------------------- 2. DC, balance, quiet, fs
    x = signals.sine(40.0, FS, 3.0, -12.0)
    dc_clean = float(np.mean(harmonic.harmonic(x, FS, amount_pct=100.0, k_max=5)[FS:2 * FS]))
    xo = x + 0.03
    y_off = harmonic.harmonic(xo, FS, amount_pct=100.0, k_max=5)
    e_o = harmonic.amplitude_envelope(xo, FS)
    raw = e_o * harmonic.chebyshev_sum(np.clip(xo / e_o, -1, 1), 100.0, 0.0, 5)
    rec.info("sine on a +0.03 DC offset: mean before / after the 15 Hz high-pass", (float(np.mean(raw[FS:2 * FS])), float(np.mean(y_off[FS:2 * FS]))))
    rec.check("T-HARM-01: DC after cleaning (sine)", db(abs(dc_clean)), "<=", -80.0, "dBFS")
    rec.check("T-HARM-01: DC after cleaning (sine on a DC offset)", db(abs(float(np.mean(y_off[FS:2 * FS])))), "<=", -80.0, "dBFS")
    for bal, fam, ref in ((100.0, (1, 3), (2, 4)), (-100.0, (2, 4), (1, 3))):
        a = util.harmonic_amps(harmonic.harmonic(x, FS, amount_pct=100.0, balance=bal, k_max=5)[FS:2 * FS], FS, 40.0, 5)
        rec.check(f"balance {bal:+.0f}: suppressed family below the other", db(max(a[i] for i in fam) / min(a[i] for i in ref)), "<=", -60.0, "dB")
    quiet = harmonic.harmonic(signals.sine(40.0, FS, 3.0, -90.0), FS, amount_pct=100.0, k_max=5)
    rec.check("-90 dBFS in: output level", db(util.rms(quiet[int(1.5 * FS):])), "<=", -110.0, "dBFS")
    fs_err = {}
    for fs in (44100, 48000, 96000, 192000):
        xs = signals.sine(40.0, fs, 3.0, level_db=-12.0)
        fs_err[fs] = round(max(abs(e) for e in weight_errors(harmonic.harmonic(xs, fs, amount_pct=100.0, k_max=5), 40.0, 10 ** (-12 / 20), fs)[0]), 3)
    rec.info("weights error by sample rate [dB]", fs_err)
    rec.check("sample-rate independence: worst weights error", max(fs_err.values()), "<=", 0.5, "dB")

    # ---------------------------------------------------------------- 3. two-tone IMD (SIG-17, informational)
    f1, f2 = 50.0, 70.0
    x2 = signals.two_tone(f1, f2, FS, 3.0, level_db=-12.0)
    y2 = harmonic.harmonic(x2, FS, amount_pct=100.0, k_max=5)
    seg = y2[FS:2 * FS]  # 1 s = integer cycles of 10 Hz
    spec = np.abs(np.fft.rfft(seg)) * 2.0 / len(seg)
    lines = {int(f): float(spec[int(f)]) for f in range(10, 400, 10) if spec[int(f)] > 1e-5}  # lines are multiples of 10 Hz
    harmonic_lines = {int(m * f): lines.get(int(m * f), 0.0) for m in range(2, 6) for f in (f1, f2) if m * f < 400}
    harm_pow = sum(v ** 2 for v in harmonic_lines.values())
    all_pow = sum(v ** 2 for v in lines.values())
    imd_lines = {f: round(db(v), 1) for f, v in sorted(lines.items(), key=lambda kv: -kv[1]) if f not in harmonic_lines}
    rec.info("SIG-17 (50 + 70 Hz, -12 dBFS peak): output lines [dBFS], strongest first",
             {f: round(db(v), 1) for f, v in sorted(lines.items(), key=lambda kv: -kv[1])[:14]})
    rec.info("SIG-17: intermodulation lines (not k*f1 or k*f2) [dBFS]", dict(list(imd_lines.items())[:10]))
    rec.info("SIG-17: power in intermodulation lines relative to all output lines [dB]", round(10 * np.log10(max(all_pow - harm_pow, 1e-300) / all_pow), 1))

    # ---------------------------------------------------------------- 4. 808 onset and tail
    x808 = band.range_filter(signals.bass_808(FS), FS, 25.0, 250.0)
    y808 = harmonic.harmonic(x808, FS, amount_pct=100.0, k_max=4)
    e808 = harmonic.amplitude_envelope(x808, FS)
    t = np.arange(len(x808)) / FS
    rows = {}
    for lo, hi in ((0.0, 0.05), (0.05, 0.1), (0.1, 0.2), (0.2, 0.4), (0.4, 0.6), (0.6, 0.65), (0.65, 0.7), (0.7, 0.8)):
        m = (t >= lo) & (t < hi)
        rows[f"{int(lo*1000)}-{int(hi*1000)} ms"] = round(db(util.rms(y808[m]) / max(util.rms(x808[m]), 1e-12)), 1)
    rec.info("808: harmonic output level relative to the dry band signal [dB] by time (steady state: constant)", rows)
    first = db(np.max(np.abs(y808[:int(0.01 * FS)])) / np.max(np.abs(y808[int(0.3 * FS):int(0.5 * FS)])))
    rec.info("808: peak of the first 10 ms relative to the sustain [dB] (clamped normaliser before the envelope has risen)", round(first, 1))

    # ---------------------------------------------------------------- plots
    fig, ax = plt.subplots(2, 3, figsize=(16, 8.5))
    xs = signals.sine(55.0, FS, 3.0, -12.0)
    k = np.arange(2, 6)
    for name, y, style in (("max-hold envelope", harmonic.harmonic(xs, FS, amount_pct=100.0, k_max=5), "o-"), ("SPEC-literal follower", literal(xs, FS), "s--")):
        a = util.harmonic_amps(y[FS:FS + FS * 55 // 55], FS, 55.0, 5)
        ax[0, 0].plot(k, [db(a[i - 1] / 10 ** (-12 / 20)) for i in k], style, label=name)
    ax[0, 0].plot(k, [-db(i) for i in k], "k:", label="w_k = 1/k")
    ax[0, 0].set(xlabel="harmonic k", ylabel="level re envelope [dB]", title="Weights at 55 Hz, -12 dBFS, amount 100 %")
    ax[0, 0].legend(fontsize=7); ax[0, 0].grid(alpha=.3)
    ax[0, 1].plot(freqs, [max(err_new[(f, lv)] for lv in levels) for f in freqs], "o-", label="max-hold envelope")
    ax[0, 1].plot(freqs, [max(err_lit[(f, lv)] for lv in levels) for f in freqs], "s--", label="SPEC-literal follower")
    ax[0, 1].axhline(0.5, color="gray", lw=0.6)
    ax[0, 1].set(xlabel="Hz", ylabel="worst weight error [dB]", title="Weight error vs frequency (all levels)")
    ax[0, 1].legend(fontsize=7); ax[0, 1].grid(alpha=.3)
    ax[0, 2].vlines(list(lines), 0, [db(v) + 140 for v in lines.values()], color="C0")
    ax[0, 2].vlines([f for f in lines if f not in harmonic_lines], 0, [db(v) + 140 for f, v in lines.items() if f not in harmonic_lines], color="C3", label="intermodulation")
    ax[0, 2].set(xlabel="Hz", ylabel="dB re -140 dBFS", xlim=(0, 400), title="SIG-17: 50 + 70 Hz through HARMONIC (red: IMD)")
    ax[0, 2].legend(fontsize=7); ax[0, 2].grid(alpha=.3)
    ax[1, 0].plot(t * 1000, x808, lw=0.8, label="x_b")
    ax[1, 0].plot(t * 1000, e808, "k", lw=0.8, label="envelope e")
    ax[1, 0].plot(t * 1000, y808, lw=0.8, label="harmonics")
    ax[1, 0].set(xlabel="ms", xlim=(0, 800), title="808 through HARMONIC (amount 100 %, K = 4)")
    ax[1, 0].legend(fontsize=7); ax[1, 0].grid(alpha=.3)
    bals = np.linspace(-100, 100, 21)
    amps = np.array([util.harmonic_amps(harmonic.harmonic(x, FS, amount_pct=100.0, balance=b, k_max=5)[FS:2 * FS], FS, 40.0, 5) for b in bals])
    for i in range(1, 5):
        ax[1, 1].plot(bals, 20 * np.log10(np.maximum(amps[:, i] / 10 ** (-12 / 20), 1e-9)), label=f"k = {i + 1}")
    ax[1, 1].set(xlabel="balance", ylabel="dB re envelope", ylim=(-60, 0), title="Even / odd balance")
    ax[1, 1].legend(fontsize=7); ax[1, 1].grid(alpha=.3)
    ee = harmonic.amplitude_envelope(xs, FS)[FS:FS + 2000]
    el = envelope.peak_follower(xs, FS, 5.0, 60.0)[FS:FS + 2000]
    ax[1, 2].plot(np.arange(2000) / FS * 1000, ee / 10 ** (-12 / 20), label="max-hold (1.5 periods of 30 Hz) + 5/60 ms")
    ax[1, 2].plot(np.arange(2000) / FS * 1000, el / 10 ** (-12 / 20), label="SPEC-literal 5/60 ms follower")
    ax[1, 2].set(xlabel="ms", ylabel="envelope / amplitude", title="Amplitude estimate of a 55 Hz sine")
    ax[1, 2].legend(fontsize=7); ax[1, 2].grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(rec.fig_path("harmonic"), dpi=100)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
