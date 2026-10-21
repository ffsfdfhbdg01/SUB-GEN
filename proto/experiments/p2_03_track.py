# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-03: TRACK pitch detectors (MPM / YIN / PLL) - accuracy, octave errors, lock time, lag vs window, L_look."""
import math

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy import signal as sps

from experiments._common import Recorder
from subgen_proto import band, signals, svf, track, util

FS = 48000
LO, HI, FMIN = 25.0, 250.0, 25.0  # source band and search floor of the study (SIG-01 reaches 30 Hz)
WINDOWS = (1.5, 2.0, 2.5, 3.0)
CENTS_0_5_PCT = 8.7


def xb(x, lo=LO, hi=HI):
    return band.range_filter(x, FS, lo, hi)


def const(f):
    return lambda t: np.full(np.shape(t), float(f))


def midi_hz(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def truth_808(f_end, glide_from, glide_ms=80.0, dur=0.8):
    tau = glide_ms / 1000.0 / 3.0

    def truth(t):
        t = np.asarray(t, dtype=float)
        return np.where((t >= 0.0) & (t < dur), f_end + (glide_from - f_end) * np.exp(-np.maximum(t, 0.0) / tau), np.nan)

    return truth


def truth_glide(t):
    return 80.0 * 2.0 ** (-np.clip((np.asarray(t, dtype=float) - 0.1) / 0.2, 0.0, 1.0))


def glide_signal(dur=0.6):
    t = np.arange(int(dur * FS)) / FS
    return 0.25 * np.sin(2.0 * np.pi * np.cumsum(truth_glide(t)) / FS)


def pll_track(x, **kw):
    f_hz, ph = track.pll(x, FS, **kw)
    idx = np.arange(0, len(x), 96)  # 2 ms
    return track.Track(idx / FS, f_hz[idx], np.ones(len(idx))), ph


def detect(name, x, window_periods=2.5, f_min=FMIN, **kw):
    if name == "PLL" and "f_init" not in kw:
        kw["f_init"] = 60.0
    if name == "MPM":
        return track.mpm(x, FS, f_min=f_min, window_periods=window_periods, **kw)
    if name == "YIN":
        return track.yin(x, FS, f_min=f_min, window_periods=window_periods, **kw)
    return pll_track(x, **kw)[0]


def agg(evs):
    """Worst/median figures over a list of evaluate() results."""
    lock = np.array([e["lock_time_s"] for e in evs], dtype=float)
    lock = lock[np.isfinite(lock)]
    cm = np.array([e["cents_median"] for e in evs], dtype=float)
    return {"n": len(evs), "worst_cents_median": float(np.nanmax(cm)) if np.isfinite(cm).any() else math.nan,
            "unlocked": int(len(evs) - len(lock)), "octave_errors": int(sum(e["octave_errors"] for e in evs)),
            "lock_ms_median": round(1000 * float(np.median(lock)), 1) if lock.size else math.nan,
            "lock_ms_max": round(1000 * float(np.max(lock)), 1) if lock.size else math.nan,
            "lag_ms_median": round(1000 * float(np.median([e["lag_s"] for e in evs])), 1)}


def main():
    rec = Recorder("p2_03")
    detectors = ("MPM", "YIN", "PLL")

    # ---------------------------------------------------------------- 1. signal classes x detectors
    classes = {}
    sig01 = [(f, lvl) for f in (30.0, 40.0, 55.0, 80.0, 120.0, 200.0) for lvl in (-6.0, -18.0, -40.0)]
    classes["SIG-01 sines"] = [(xb(signals.sine(f, FS, 2.0, lvl)), const(f), 0.0) for f, lvl in sig01]
    notes = range(28, 48)  # E1 ... B2
    classes["SIG-04 808"] = [(xb(signals.bass_808(FS, f_end=midi_hz(m), glide_from=2 * midi_hz(m), harm2_db=-8.0)),
                              truth_808(midi_hz(m), 2 * midi_hz(m)), 0.0) for m in notes]
    classes["SIG-05 pluck"] = [(xb(signals.bass_pluck(FS, midi_hz(m))), const(midi_hz(m)), 0.0) for m in range(28, 44)]
    sig19 = []
    for snr in (20, 10, 3):
        for seed in (2, 3, 4):
            sig19.append((xb(signals.sine_in_noise(60.0, FS, 3.0, snr, seed=seed)), const(60.0), 0.0))
    classes["SIG-19 sine + white noise (20/10/3 dB)"] = sig19

    table = {}
    for cname, items in classes.items():
        for det in detectors:
            evs = [track.evaluate(detect(det, x, **({"f_init": float(truth(np.array([onset]))[0])} if det == "PLL" else {})),
                                  truth, onset) for x, truth, onset in items]
            table[(cname, det)] = agg(evs)
    for (cname, det), a in table.items():
        rec.info(f"{cname} | {det}", a)

    for det in ("MPM", "YIN"):
        core = [table[(c, det)] for c in ("SIG-01 sines", "SIG-04 808", "SIG-05 pluck")]
        worst = max(a["worst_cents_median"] for a in core)
        octs = sum(a["octave_errors"] for a in core)
        unlocked = sum(a["unlocked"] for a in core)
        if det == "MPM":
            rec.check("MPM SIG-01/04/05: worst median error", worst, "<=", CENTS_0_5_PCT, "cents")
            rec.check("MPM SIG-01/04/05: octave errors (conf >= 0.5 frames)", octs, "==", 0)
            rec.check("MPM SIG-01/04/05: signals that never lock", unlocked, "==", 0)
        else:
            rec.info("YIN SIG-01/04/05: worst median error [cents] / octave errors / never locked", (worst, octs, unlocked))
    # 1b. D8 M10: frames > 600 cents off as the oscillator uses them, without the fitted lag: Mix reads the track 45 ms
    #     ahead (log), Live holds the last frame. These are glide-tracking errors, not a wrong octave choice.
    rt = {}
    for label, kw, ahead in (("Mix (45 ms ahead)", dict(lookahead_s=0.045, interp="log"), 0.045), ("Live (hold)", dict(interp="hold"), 0.0)):
        n_bad, worst, n_all = 0, 0.0, 0
        for x, truth, _ in classes["SIG-04 808"]:
            trk = track.mpm(x, FS, f_min=FMIN)
            f_used = track.f0_per_sample(trk, len(x), FS, **kw)
            conf_used = np.interp(np.arange(len(x)) / FS + ahead, trk.t, trk.conf)
            idx = np.clip(np.round(trk.t * FS).astype(int), 0, len(x) - 1)
            ok = (conf_used[idx] >= 0.5) & np.isfinite(f_used[idx])
            err = np.abs(1200.0 * np.log2(f_used[idx][ok] / truth(trk.t[ok])))
            n_bad += int(np.sum(err > 600.0))
            n_all += int(np.sum(ok))
            worst = max(worst, float(np.max(err, initial=0.0)))
        rt[label] = {"frames > 600 cents (20 notes)": n_bad, "frames evaluated": n_all, "worst [cents]": round(worst, 1)}
    rec.info("SIG-04 808, MPM as the oscillator uses it (no lag fitted): frames > 600 cents and the worst error", rt)
    core = [table[(c, "PLL")] for c in ("SIG-01 sines", "SIG-04 808", "SIG-05 pluck")]
    rec.info("PLL (seeded with the true f at the onset) SIG-01/04/05: worst median error [cents] / frames > 600 cents / never locked",
             (max(a["worst_cents_median"] for a in core), sum(a["octave_errors"] for a in core),
              sum(a["unlocked"] for a in core)))

    # ---------------------------------------------------------------- 2. in-band noise sweep (as P2-02)
    sweep = {}
    for snr in (30, 20, 10, 7, 5, 3, 0):
        row = {}
        for det in ("MPM", "YIN"):
            opn, err = [], []
            for seed in (5, 6, 7, 8):
                s = signals.sine(60.0, FS, 4.0, -12.0)
                nz = signals.pink_noise(FS, 4.0, 30.0, 120.0, level_db=0.0, seed=seed)
                x = s + nz * ((util.rms(s) / 10 ** (snr / 20.0)) / util.rms(nz))
                trk = detect(det, x)
                m = trk.conf >= 0.5
                opn.append(float(np.mean(m[len(m) // 4:])))
                e = np.abs(1200 * np.log2(trk.f0[m] / 60.0)) if m.any() else np.array([math.nan])
                err.append(float(np.nanmedian(e)))
            row[det] = (round(float(np.mean(opn)), 3), round(float(np.nanmean(err)), 2))
        sweep[snr] = row
    rec.info("in-band SNR -> {detector: (frames with conf >= 0.5, median error of those frames [cents])}", sweep)
    rec.check("MPM in-band SNR 20 dB: gate open fraction", sweep[20]["MPM"][0], ">=", 0.9)
    rec.check("MPM in-band SNR 0 dB: gate open fraction", sweep[0]["MPM"][0], "<=", 0.1)
    noise_conf = {}
    for wp in WINDOWS:
        vals = []
        for seed in (1, 2, 3):
            trk = track.mpm(signals.pink_noise(FS, 3.0, 30.0, 120.0, level_db=-12.0, seed=seed), FS, f_min=30.0,
                            window_periods=wp)
            vals.append((float(np.median(trk.conf[len(trk.conf) // 4:])),
                         float(np.mean(trk.conf[len(trk.conf) // 4:] >= 0.5))))
        noise_conf[wp] = tuple(round(float(v), 3) for v in np.mean(vals, axis=0))
    rec.info("pink noise 30-120 Hz, MPM: window periods -> (median conf, fraction of frames >= 0.5)", noise_conf)
    rec.check("MPM pink noise: median confidence (2.5 periods)", noise_conf[2.5][0], "<", 0.5)

    # ---------------------------------------------------------------- 3. window study
    win = {}
    for det in ("MPM", "YIN"):
        for wp in WINDOWS:
            lag = track.evaluate(detect(det, glide_signal(), wp, f_min=30.0), truth_glide, 0.0)["lag_s"]
            onsets = {}
            for f in (40.0, 55.0):
                x = np.concatenate([np.zeros(int(0.2 * FS)), signals.sine(f, FS, 0.6, -12.0)])
                ev = track.evaluate(detect(det, x, wp), lambda t, f=f: np.where(np.asarray(t) >= 0.2, f, np.nan), 0.2)
                onsets[f] = (round(1000 * ev["lock_time_s"], 1), round(1000 * ev["conf_open_s"], 1))
            freqs = (30.0, 40.0, 55.0, 80.0, 120.0, 200.0)
            sines = [track.evaluate(detect(det, xb(signals.sine(f, FS, 2.0, -18.0)), wp), const(f), 0.0) for f in freqs]
            undetected = [f for f, e in zip(freqs, sines) if not math.isfinite(e["lock_time_s"])]
            e808 = [track.evaluate(detect(det, x, wp), truth, 0.0) for x, truth, _ in classes["SIG-04 808"]]
            win[(det, wp)] = {"W_ms (f_min 25)": round(1000 * math.ceil(wp * 2000 / FMIN) / 2000, 1),
                              "W_ms (f_min 30)": round(1000 * math.ceil(wp * 2000 / 30.0) / 2000, 1),
                              "glide_lag_ms (f_min 30)": round(1000 * lag, 1),
                              "onset (lock, conf-open) ms @40/55 Hz": onsets,
                              "sines never locked (Hz)": undetected,
                              "sine worst cents (locked)": round(max(e["cents_median"] for e in sines
                                                                     if math.isfinite(e["lock_time_s"])), 2),
                              "808 octave errors": sum(e["octave_errors"] for e in e808),
                              "808 worst cents": round(max(e["cents_median"] for e in e808), 2)}
    for k, v in win.items():
        rec.info(f"window study {k[0]} {k[1]} periods", v)
    lag_25 = track.evaluate(track.mpm(glide_signal(), FS, f_min=30.0), truth_glide, 0.0)["lag_s"]
    rec.check("MPM glide lag, f_min 30 Hz, 2.5 periods (W = 83 ms)", 1000 * lag_25, "<", 60.0, "ms")

    # ---------------------------------------------------------------- 4. kick pitch drop (SIG-03)
    kick = signals.kick(FS, n_hits=1, spacing=0.6, level_db=-6.0)

    def truth_kick(t):
        t = np.asarray(t, dtype=float)
        return np.where((t >= 0.0) & (t < 0.6), 45.0 + 105.0 * np.exp(-np.maximum(t, 0.0) / 0.030), np.nan)

    kick_res = {}
    for det in ("MPM", "YIN"):
        for wp in (1.5, 2.5):
            trk = detect(det, xb(kick), wp)
            ev = track.evaluate(trk, truth_kick, 0.0)
            kick_res[(det, wp)] = {k: (round(v, 4) if isinstance(v, float) else v) for k, v in ev.items()}
    for k, v in kick_res.items():
        rec.info(f"kick 150->45 Hz (tau 30 ms) {k[0]} {k[1]} periods", v)

    # ---------------------------------------------------------------- 5. onset latency -> L_look
    dec = svf.design_butter(8, 0.4 * 2000.0, FS, "low")
    dec_delay_ms = 1000 * float(svf.group_delay_s(dec, [50.0], FS)[0])
    rec.info("8th-order Butterworth decimation filter at 800 Hz: group delay at 50 Hz [ms]", round(dec_delay_ms, 2))
    onset_tab = {}
    for f in (30.0, 35.0, 40.0, 41.2, 45.0, 55.0, 65.0, 80.0, 100.0, 120.0):
        x = xb(np.concatenate([np.zeros(int(0.2 * FS)), signals.sine(f, FS, 0.6, -12.0)]))
        ev = track.evaluate(track.mpm(x, FS, f_min=FMIN), lambda t, f=f: np.where(np.asarray(t) >= 0.2, f, np.nan), 0.2)
        onset_tab[f] = {"lock_ms": round(1000 * ev["lock_time_s"], 1), "conf_open_ms": round(1000 * ev["conf_open_s"], 1),
                        "excess_over_2_periods_ms": round(1000 * ev["conf_open_s"] - 2000.0 / f, 1)}
    rec.info("onset after silence (through the range filter), MPM 2.5 periods: f -> lock / conf-open / excess over 2 periods",
             onset_tab)

    def l_look_for(f_floor):
        need = max(v["conf_open_ms"] for f, v in onset_tab.items() if f >= f_floor) + dec_delay_ms
        return int(5 * math.ceil(need / 5.0))

    l_mix, l_print = l_look_for(55.0), l_look_for(41.2)
    rec.info("recommended L_look [ms]: Mix (notes >= 55 Hz = A1), Print (notes >= 41.2 Hz = E1)", (l_mix, l_print))
    rec.info("l_look_ms", {"mix": l_mix, "print": l_print, "live": 0})
    # 2 periods + decimation lattice (1 ms) + hop (2 ms) + first-sample trim (<= 1 ms)
    rec.check("MPM onset: confidence opens within 2 periods + 4 ms (all f, source onset to conf >= 0.5)",
              max(v["excess_over_2_periods_ms"] for v in onset_tab.values()), "<=", 4.0, "ms")

    # ---------------------------------------------------------------- 6. oscillator
    x = signals.sine(55.0, FS, 2.0, -12.0)
    trk = track.mpm(x, FS, f_min=FMIN)
    zc = {}
    for label, kw in (("Mix (log, +W/2)", dict(lookahead_s=trk.window_s / 2, interp="log")), ("Live (hold)", dict(interp="hold"))):
        f0 = track.f0_per_sample(trk, len(x), FS, **kw)
        osc = track.oscillator(f0 / 2.0, FS, 0.0)
        zc[label] = abs(util.zc_freq(osc[FS // 2:], FS) - 27.5) / 27.5 * 100
    rec.info("oscillator at f0/2 of a 55 Hz sine: frequency error [%]", zc)
    rec.check("oscillator end-to-end frequency error (Mix)", zc["Mix (log, +W/2)"], "<=", 0.1, "%")
    fs44, f_tri, n44 = 44100, 97.3, 2 * 44100
    tri = track.oscillator(np.full(n44, f_tri), fs44, 100.0)
    k_max = int((fs44 / 2) // f_tri)
    phi = 2 * np.pi * f_tri * np.arange(n44) / fs44
    ref = sum((8 / np.pi ** 2) * (-1) ** ((k - 1) // 2) * np.sin(k * phi) / k ** 2 for k in range(1, k_max + 1, 2))
    alias_db = util.band_db(tri - ref, fs44, 0.0, 500.0) - util.band_db(tri, fs44, 0.0, 500.0)
    rec.info("naive triangle 97.3 Hz @ 44.1 kHz: alias level in 0-500 Hz relative to the signal [dB]", round(alias_db, 1))
    rec.check("triangle oscillator: alias below 500 Hz", alias_db, "<=", -90.0, "dB")

    # ---------------------------------------------------------------- 7. PLL vs analytic phase
    wrap = lambda a: np.angle(np.exp(1j * a))
    phase_tab = {}
    t = np.arange(int(3.0 * FS)) / FS
    clean = signals.sine(55.0, FS, 3.0, -12.0)
    true_phase = 2 * np.pi * 55.0 * t - np.pi / 2
    seg = slice(int(1.5 * FS), int(2.5 * FS))
    nz = signals.pink_noise(FS, 3.0, 30.0, 120.0, level_db=0.0, seed=5)
    noisy = clean + nz * ((util.rms(clean) / 10 ** (10 / 20.0)) / util.rms(nz))
    for bw in (2.5, 5.0, 10.0, 20.0):
        row = {}
        for label, x in (("clean", clean), ("in-band SNR 10 dB", noisy)):
            f_hz, ph = track.pll(x, FS, f_init=60.0, bw_hz=bw)
            row[label] = round(float(np.degrees(np.sqrt(np.mean(wrap(ph - true_phase)[seg] ** 2)))), 2)
        lock = track.evaluate(pll_track(clean, bw_hz=bw)[0], const(55.0), 0.0)["lock_time_s"]
        row["lock time [ms]"] = round(1000 * lock, 0)
        gl = glide_signal()
        f_hz, ph = track.pll(gl, FS, f_init=80.0, bw_hz=bw)
        tg = np.arange(len(gl)) / FS
        tphase = 2 * np.pi * np.cumsum(truth_glide(tg)) / FS - np.pi / 2
        row["glide 80->40 Hz: max |phase err| after 0.1 s [deg]"] = round(
            float(np.degrees(np.max(np.abs(wrap(ph - tphase)[int(0.1 * FS):])))), 1)
        phase_tab[bw] = row
    ph_h = np.unwrap(np.angle(sps.hilbert(noisy)))
    phase_tab["Hilbert"] = {"in-band SNR 10 dB": round(float(np.degrees(np.sqrt(np.mean(wrap(ph_h - true_phase)[seg] ** 2)))), 2)}
    rec.info("PLL bandwidth -> rms phase error vs the clean source phase [deg]", phase_tab)
    rec.check("PLL (5 Hz) phase error, clean 55 Hz", phase_tab[5.0]["clean"], "<=", 15.0, "deg")

    # ---------------------------------------------------------------- recommendation
    rec.info("recommendation: detector", "MPM on onset-trimmed frames (calibrated clarity as confidence) with the "
             "continuity prior; YIN is the fallback; PLL only for phase locking of steady material")
    rec.info("recommendation: L_look [ms]", {"Live": 0, "Mix": l_mix, "Print": l_print})

    # ---------------------------------------------------------------- plots
    fig, ax = plt.subplots(2, 3, figsize=(16, 8.5))
    m = 28
    f_note = midi_hz(m)
    x = xb(signals.bass_808(FS, f_end=f_note, glide_from=2 * f_note, harm2_db=-8.0))
    tt = np.arange(0, 0.4, 0.002)
    ax[0, 0].plot(tt, truth_808(f_note, 2 * f_note)(tt), "k", lw=2, label="truth")
    for det in detectors:
        trk = detect(det, x)
        ax[0, 0].plot(trk.t, trk.f0, label=det)
    ax[0, 0].set(xlim=(0, 0.4), ylim=(0, 260), xlabel="s", ylabel="Hz", title=f"808 E1 ({f_note:.1f} Hz), glide from 2f")
    ax[0, 0].legend(fontsize=7); ax[0, 0].grid(alpha=.3)
    w30 = [win[("MPM", w)]["W_ms (f_min 30)"] for w in WINDOWS]
    for det in ("MPM", "YIN"):
        ax[0, 1].plot(w30, [win[(det, w)]["glide_lag_ms (f_min 30)"] for w in WINDOWS], "o-", label=det)
    ax[0, 1].plot(w30, [w / 2 for w in w30], "k--", lw=0.8, label="W/2")
    ax[0, 1].set(xlabel="window W, f_min = 30 Hz [ms]", ylabel="lag [ms]", title="Lag on the 80->40 Hz glide")
    ax[0, 1].legend(fontsize=7); ax[0, 1].grid(alpha=.3)
    fs_o = sorted(onset_tab)
    ax[0, 2].plot(fs_o, [onset_tab[f]["conf_open_ms"] for f in fs_o], "o-", label="confidence opens")
    ax[0, 2].plot(fs_o, [onset_tab[f]["lock_ms"] for f in fs_o], "s--", label="pitch locks")
    ax[0, 2].plot(fs_o, [2.06 * 1000 / f for f in fs_o], "k:", label="2.06 periods")
    for L in (25, 50):
        ax[0, 2].axhline(L, color="gray", lw=0.6)
    ax[0, 2].set(xlabel="note frequency [Hz]", ylabel="ms after onset", title="Onset after silence (MPM, 2.5 periods)")
    ax[0, 2].legend(fontsize=7); ax[0, 2].grid(alpha=.3)
    snrs = sorted(sweep)
    for det in ("MPM", "YIN"):
        ax[1, 0].plot(snrs, [sweep[s][det][0] for s in snrs], "o-", label=det)
    ax[1, 0].set(xlabel="in-band SNR [dB]", ylabel="frames with conf >= 0.5", title="Confidence gate vs in-band noise")
    ax[1, 0].legend(fontsize=7); ax[1, 0].grid(alpha=.3)
    bws = [2.5, 5.0, 10.0, 20.0]
    ax[1, 1].plot(bws, [phase_tab[b]["clean"] for b in bws], "o-", label="PLL clean")
    ax[1, 1].plot(bws, [phase_tab[b]["in-band SNR 10 dB"] for b in bws], "s-", label="PLL SNR 10 dB")
    ax[1, 1].axhline(phase_tab["Hilbert"]["in-band SNR 10 dB"], color="r", ls="--", label="Hilbert SNR 10 dB")
    ax[1, 1].set(xlabel="PLL bandwidth [Hz]", ylabel="rms phase error [deg]", title="Phase tracking (55 Hz)", yscale="log")
    ax[1, 1].legend(fontsize=7); ax[1, 1].grid(alpha=.3, which="both")
    tk = np.arange(0, 0.5, 0.002)
    ax[1, 2].plot(tk, truth_kick(tk), "k", lw=2, label="truth")
    for (det, wp), _ in kick_res.items():
        trk = detect(det, xb(kick), wp)
        ax[1, 2].plot(trk.t, trk.f0, label=f"{det} {wp}")
    ax[1, 2].set(xlim=(0, 0.5), ylim=(0, 260), xlabel="s", ylabel="Hz", title="Kick 150->45 Hz (tau 30 ms): tracker lags")
    ax[1, 2].legend(fontsize=7); ax[1, 2].grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(rec.fig_path("track"), dpi=100)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
