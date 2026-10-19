# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-04: TRIGGER engine - repeatability, curve accuracy, onset time vs fractional position, retrigger continuity."""
import math

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy import signal as sps
from scipy.optimize import brentq

from experiments._common import Recorder
from subgen_proto import envelope, signals, trigger, util

FS = 48000


def kick_at(fs, t0, dur=0.3, amp=0.5, f_start=150.0, f_end=45.0, tau=0.030, t60=0.300):
    """The SIG-03 kick formula evaluated for a hit starting at the arbitrary time t0 (s): sub-sample positions."""
    t = np.arange(int(dur * fs)) / fs - t0
    tt = np.maximum(t, 0.0)
    phi = 2.0 * np.pi * (f_end * tt + (f_start - f_end) * tau * (1.0 - np.exp(-tt / tau)))
    return np.where(t >= 0.0, amp * 10.0 ** (-3.0 * tt / t60) * np.sin(phi), 0.0)


def crossing_reference(t0, thresh_db=-30.0, **kw):
    """Continuous-envelope crossing of the same hit, found on a 64x finer grid (samples at FS)."""
    xf = kick_at(64 * FS, t0, dur=t0 + 0.02, **kw)
    ef = envelope.peak_follower(xf, 64 * FS, trigger.ATTACK_MS, trigger.RELEASE_MS)
    th = 10.0 ** (thresh_db / 20.0)
    i = int(np.argmax(ef >= th))
    return ((i - 1) + (th - ef[i - 1]) / (ef[i] - ef[i - 1])) / 64.0


def bursts(n_bursts, spacing_s, amp=0.9, length_ms=10.0):
    x = np.zeros(int(round(n_bursts * spacing_s * FS)) + int(0.3 * FS))
    n = int(length_ms * 1e-3 * FS)
    burst = amp * np.sin(2.0 * np.pi * 100.0 * np.arange(n) / FS)
    for k in range(n_bursts):
        s = int(round(k * spacing_s * FS))
        x[s:s + n] = burst
    return x


def main():
    rec = Recorder("p2_04")

    # ---------------------------------------------------------------- 1. repeatability (T-TRIG-02)
    x = signals.kick(FS, n_hits=100, spacing=0.5, level_db=-6.0)
    on = trigger.detect_onsets(x, FS)
    y = trigger.render(x, FS)
    hit = int(0.5 * FS)
    seg = lambda i: y[i * hit:(i + 1) * hit]
    worst = max(util.null_db(seg(1), seg(i)) for i in range(2, 100))
    rec.info("100 identical kicks: detected onsets", len(on))
    rec.check("100 kicks: onsets detected", len(on), "==", 100)
    rec.check("100 identical kicks: worst null between voices 2..99 and voice 1", worst, "<=", -120.0, "dB")
    rec.info("100 identical kicks: first hit (no predecessor tail) vs voice 1 [dB]", round(util.null_db(seg(1), seg(0)), 1))

    # ---------------------------------------------------------------- 2. curves (T-TRIG-03)
    curve_rows = {}
    for label, kw in (("default", {}), ("120->30 Hz, 10 ms, T60 1 s", dict(f_start=120.0, f_end=30.0, tau_p_ms=10.0, t60_ms=1000.0)),
                      ("40->60 Hz rising, attack 5 ms", dict(f_start=40.0, f_end=60.0, attack_ms=5.0)),
                      ("200->100 Hz, 100 ms, T60 150 ms", dict(f_start=200.0, f_end=100.0, tau_p_ms=100.0, t60_ms=150.0))):
        p = {**trigger.DEFAULTS, **kw}
        y1, _ = trigger.voice(FS, int(0.4 * FS), **kw)
        t = np.arange(len(y1)) / FS
        zc = np.sort(np.concatenate([util.zero_crossings(y1, True), util.zero_crossings(y1, False)])) / FS
        tau_p = p["tau_p_ms"] * 1e-3
        phase = lambda tt: 2.0 * np.pi * (p["f_end"] * tt + (p["f_start"] - p["f_end"]) * tau_p * (1.0 - np.exp(-tt / tau_p)))
        # The k-th zero crossing of sin(phase) sits where phase = k pi. Comparing the measured half periods with the
        # formula's own half periods is unbiased for fast chirps (a midpoint comparison is off by ~1.7 % at 10 ms).
        exact = np.array([brentq(lambda tt, m=round(phase(z) / np.pi): phase(tt) - m * np.pi, z - 0.003, z + 0.003) for z in zc])
        ferr_all = np.abs((zc[1:] - zc[:-1]) / (exact[1:] - exact[:-1]) - 1.0)  # f ~ 1 / half period
        ferr = float(np.max(ferr_all[zc[:-1] >= 0.003]) * 100)
        pk, _ = sps.find_peaks(np.abs(y1))
        ta, t60 = p["attack_ms"] * 1e-3, p["t60_ms"] * 1e-3
        pk = pk[(t[pk] >= max(0.003, ta + 1e-3)) & (y1[pk] != 0.0) & (np.abs(y1[pk]) > 10 ** (-40 / 20))]
        aerr = float(np.max(np.abs(20 * np.log10(np.abs(y1[pk]) / 10 ** (-3 * (t[pk] - ta) / t60)))))
        curve_rows[label] = (round(ferr, 4), round(aerr, 4))
    rec.info("curve accuracy: (max frequency error [%], max level error [dB]) per parameter set", curve_rows)
    rec.check("voice frequency curve (worst of 4 sets)", max(v[0] for v in curve_rows.values()), "<=", 0.5, "%")
    rec.check("voice level curve (worst of 4 sets)", max(v[1] for v in curve_rows.values()), "<=", 0.1, "dB")

    # ---------------------------------------------------------------- 3. onset time vs fractional position
    deltas = np.arange(16) / 16.0
    err, latency = [], []
    for d in deltas:
        t0 = 0.1 + d / FS  # a hit starting d samples after sample 4800
        t_c = float(trigger.detect_onsets(kick_at(FS, t0), FS)[0])
        err.append(t_c - crossing_reference(t0))  # both measured from the array start
        latency.append(t_c - (0.1 * FS + d))
    err = np.array(err)
    bias, jitter = float(np.mean(err)), float(np.ptp(err))
    rec.info("onset detection error vs the 64x-finer reference [samples], per fractional offset k/16", np.round(err, 4))
    rec.info("detection error: constant bias / peak-to-peak jitter [samples]", (round(bias, 4), round(jitter, 4)))
    rec.info("bias explained: the one-pole follower holds x[n] over the interval BEFORE sample n, i.e. it leads the "
             "continuous envelope by half a sample", "add +0.5 sample to the interpolated crossing to remove it")
    rec.check("onset detection error (max over 16 fractional offsets)", float(np.max(np.abs(err))), "<=", 1.0, "samples")
    rec.info("detection latency after the true hit start [ms]: min / max", (round(1000 * min(latency) / FS, 3), round(1000 * max(latency) / FS, 3)))
    rec.check("live latency of the threshold crossing (max)", 1000 * max(latency) / FS, "<=", 2.0, "ms")
    # What a timing error of D samples does to the voice: the same voice shifted by D samples, on a 64x grid.
    n64 = int(0.4 * 64 * FS)
    ref64 = trigger.voice(64 * FS, n64)[0]
    shift_null = lambda dsamp: util.null_db(ref64, trigger.voice(64 * FS, n64, t_on_frac=64.0 * dsamp)[0])
    shift_tab = {d: round(shift_null(d), 1) for d in (jitter, 0.25, 0.5, 1.0)}
    rec.info("voice vs the same voice shifted by D samples [dB]: D = measured jitter, 0.25, 0.5, 1 (integer rounding, worst)", shift_tab)
    rec.check("fractional onset: voice difference caused by the detection jitter is 30 dB below worst integer rounding",
              shift_tab[jitter] - shift_tab[1.0], "<=", -30.0, "dB")

    # ---------------------------------------------------------------- 4. retrigger sweep (T-TRIG-04)
    inc = lambda s: 2.0 * np.pi * (45.0 * s + 35.0 * 0.030 * (1.0 - np.exp(-s / 0.030)))
    ta, t60 = 0.002, 0.350
    sweep = {}
    for sp in (0.04, 0.05, 0.06, 0.08, 0.10, 0.15, 0.20, 0.30):
        xb = bursts(4, sp)
        kw = dict(thresh_db=-12.0, hold_ms=0.0, lockout_ms=5.0)
        ons = trigger.detect_onsets(xb, FS, **kw)
        yr = trigger.render(xb, FS, retrigger="restart", **kw)
        yl = trigger.render(xb, FS, retrigger="legato", **kw)
        h = int(0.001 * FS)
        sec = lambda y, o: np.diff(y[int(o) - h:int(o) + h + 1], 2)
        phase = np.unwrap(np.angle(sps.hilbert(yl)))
        # exact legato state at the second onset (see test_legato_retrigger_carries_phase_and_amplitude), vel 100 % off
        x2 = bursts(2, sp)
        t0, t1 = trigger.detect_onsets(x2, FS, **kw)
        y2 = trigger.render(x2, FS, retrigger="legato", velocity_pct=0.0, **kw)
        s_old = (t1 - t0) / FS
        a_c = 10.0 ** (-3.0 * (s_old - ta) / t60)
        k = np.arange(int(np.ceil(t1)), len(x2))
        sv = (k - t1) / FS
        amp = np.where(sv < ta, a_c + (1.0 - a_c) * sv / ta, 10.0 ** (-3.0 * (sv - ta) / t60))
        resid = float(np.max(np.abs(y2[k] - amp * np.sin(inc(s_old) + inc(sv)))))
        sweep[sp] = {"onsets": len(ons),
                     "restart click": round(max(float(np.max(np.abs(sec(yr, o)))) for o in ons[1:]), 6),
                     "legato slope kink": round(max(float(np.max(np.abs(sec(yl, o)))) for o in ons[1:]), 5),
                     "legato Hilbert phase 2nd diff [rad]": round(max(float(np.max(np.abs(sec(phase, o)))) for o in ons[1:]), 4),
                     "legato carried-state residual": resid}
    rec.info("retrigger sweep: spacing [s] -> onsets / restart click / legato slope kink / Hilbert phase / exact residual", sweep)
    rec.check("restart: worst click over the sweep", max(v["restart click"] for v in sweep.values()), "<=", 1e-3)
    rec.check("legato: phase and amplitude carried exactly (worst residual over the sweep)",
              max(v["legato carried-state residual"] for v in sweep.values()), "<=", 1e-9)
    rec.info("legato: waveform slope kink (second difference) over the sweep, full scale 1 [max]",
             max(v["legato slope kink"] for v in sweep.values()))
    rec.check("sweep: every burst detected", min(v["onsets"] for v in sweep.values()), ">=", 4)

    # ---------------------------------------------------------------- 5. velocity and follow
    levels = [-30.0, -24.0, -18.0, -12.0, -6.0, -2.0]
    vel_tab = {}
    for vel in (0.0, 30.0, 60.0, 100.0):
        peaks = []
        for lv in levels:
            xk = signals.kick(FS, n_hits=1, spacing=0.5, level_db=lv)
            yv = trigger.render(xk, FS, velocity_pct=vel, thresh_db=-40.0)
            peaks.append(round(float(np.max(np.abs(yv))), 3))
        vel_tab[vel] = peaks
    rec.info("voice peak for source peaks at -30...-2 dBFS, by velocity %", vel_tab)
    burst = np.concatenate([bursts(1, 0.05, amp=0.2, length_ms=50.0)[:int(0.05 * FS)], np.zeros(int(0.45 * FS))])
    free, fol = trigger.render(burst, FS), trigger.render(burst, FS, follow=True)
    seg_a = slice(int(0.15 * FS), int(0.20 * FS))
    rec.info("follow: level 100-150 ms after a 50 ms source, free vs follow [dB]", (round(util.db(util.rms(free[seg_a])), 1), round(util.db(util.rms(fol[seg_a])), 1)))

    # ---------------------------------------------------------------- plots
    fig, ax = plt.subplots(2, 3, figsize=(16, 8.5))
    y1, y2 = trigger.voice(FS, int(0.4 * FS))
    t = np.arange(len(y1)) / FS
    ax[0, 0].plot(t * 1000, y1, label="Sub -1")
    ax[0, 0].plot(t * 1000, y2, label="Sub -2")
    ax[0, 0].plot(t * 1000, 10 ** (-3 * (t - 0.002) / 0.35), "k:", lw=0.8, label="a(t)")
    ax[0, 0].set(xlabel="ms", xlim=(0, 250), title="Voice: 80 -> 45 Hz (30 ms), attack 2 ms, T60 350 ms")
    ax[0, 0].legend(fontsize=7); ax[0, 0].grid(alpha=.3)
    zc = np.sort(np.concatenate([util.zero_crossings(y1, True), util.zero_crossings(y1, False)]))
    ax[0, 1].plot((zc[1:] + zc[:-1]) / 2 / FS * 1000, 0.5 * FS / np.diff(zc), "o", ms=3, label="half-period of y1")
    ax[0, 1].plot(t * 1000, 45 + 35 * np.exp(-t / 0.03), "k", lw=1, label="f(t)")
    ax[0, 1].set(xlabel="ms", ylabel="Hz", xlim=(0, 250), title="Pitch curve: measured vs formula")
    ax[0, 1].legend(fontsize=7); ax[0, 1].grid(alpha=.3)
    ax[0, 2].plot(deltas, err, "o-")
    ax[0, 2].set(xlabel="fractional offset of the hit [samples]", ylabel="detection error [samples]",
                 title=f"Detection error: bias {bias:.3f}, jitter {jitter:.4f} samples", )
    ax[0, 2].grid(alpha=.3)
    dd = np.linspace(0.002, 1.2, 40)
    ax[1, 0].semilogx(dd, [shift_null(d) for d in dd], "-", label="voice vs itself shifted by D")
    ax[1, 0].plot([jitter], [shift_tab[jitter]], "o", label=f"detection jitter ({jitter:.4f} samples)")
    ax[1, 0].plot([1.0], [shift_tab[1.0]], "s", label="integer rounding, worst (1 sample)")
    ax[1, 0].set(xlabel="onset timing error D [samples]", ylabel="voice difference [dB]", title="Value of sub-sample onset alignment")
    ax[1, 0].legend(fontsize=7); ax[1, 0].grid(alpha=.3, which="both")
    xb = bursts(2, 0.10)
    kw = dict(thresh_db=-12.0, hold_ms=0.0, lockout_ms=5.0)
    o2 = trigger.detect_onsets(xb, FS, **kw)[1]
    sl = slice(int(o2) - int(0.01 * FS), int(o2) + int(0.02 * FS))
    tt = (np.arange(len(xb))[sl] - o2) / FS * 1000
    ax[1, 1].plot(tt, trigger.render(xb, FS, retrigger="restart", **kw)[sl], label="Restart")
    ax[1, 1].plot(tt, trigger.render(xb, FS, retrigger="legato", **kw)[sl], label="Legato")
    ax[1, 1].axvline(0, color="gray", lw=0.6)
    ax[1, 1].set(xlabel="ms around the second onset", title="Retrigger 100 ms after the first hit")
    ax[1, 1].legend(fontsize=7); ax[1, 1].grid(alpha=.3)
    for vel, peaks in vel_tab.items():
        ax[1, 2].plot(levels, 20 * np.log10(np.maximum(peaks, 1e-9)), "o-", label=f"vel {vel:g} %")
    ax[1, 2].set(xlabel="source peak [dBFS]", ylabel="voice peak [dBFS]", title="Velocity law V = (peak/0.5)^(vel/100), max 1.5")
    ax[1, 2].legend(fontsize=7); ax[1, 2].grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(rec.fig_path("trigger"), dpi=100)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
