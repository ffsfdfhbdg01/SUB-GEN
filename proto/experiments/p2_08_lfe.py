# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-08: LFE low-pass families, cascade with the encoder filter, linear-phase FIR length and latency."""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy import signal as sps

from experiments._common import Recorder
from subgen_proto import lfe, svf

FS = 48000
FC = 120.0
DESIGN = {"butter": svf.design_butter, "lr": svf.design_lr, "bessel": svf.design_bessel}


def options():
    opts = {}
    for order in (2, 4, 8, 16):
        opts[f"BW{6 * order}"] = ("butter", order)
        opts[f"LR{6 * order}"] = ("lr", order)  # LR order 2 = BW1 x BW1 (12 dB/oct) ... order 16 (96 dB/oct)
    for order in (2, 4, 8):
        opts[f"Bessel{6 * order}"] = ("bessel", order)
    return opts


def lp(kind, order, fc, fs=FS):
    return DESIGN[kind](order, fc, fs, "low")


def mag_db(sections, f, fs=FS):
    return 20.0 * np.log10(np.abs(svf.freqz(sections, np.atleast_1d(f), fs)))


def target(fs, hp=20.0, lp_hz=120.0):
    secs = (svf.design_butter(4, hp, fs, "high") if hp else []) + svf.design_butter(4, lp_hz, fs, "low")
    return lambda f: np.abs(svf.freqz(secs, f, fs))


def main():
    rec = Recorder("p2_08")
    opts = options()

    # ---------------------------------------------------------------- 1. LFE low-pass families at 120 Hz
    tab = {}
    for name, (kind, order) in opts.items():
        s = lp(kind, order, FC)
        gd = 1000.0 * svf.group_delay_s(s, [40.0, 80.0, 120.0], FS)
        tab[name] = {"group delay 40/80/120 Hz [ms]": tuple(round(float(v), 2) for v in gd),
                     "gain at 80 Hz / fc [dB]": (round(float(mag_db(s, 80.0)[0]), 2), round(float(mag_db(s, FC)[0]), 2)),
                     "attenuation at 1.5 fc / 2 fc [dB]": (round(lfe.out_of_band_db(kind, order, FC, FS), 2),
                                                           round(lfe.out_of_band_db(kind, order, FC, FS, ratio=2.0), 2))}
    rec.info("LFE low-pass at 120 Hz, 48 kHz: families and slopes", tab)

    # T-LFE-01: wideband noise through BW4 at 120 Hz, attenuation around 1.5 fc against the analytic value
    x = np.random.default_rng(3).standard_normal(10 * FS) * 0.1
    y = svf.process_fast(lp("butter", 4, FC), x)
    f_w, pxx = sps.welch(x, FS, nperseg=2 ** 15)
    _, pyy = sps.welch(y, FS, nperseg=2 ** 15)
    band = (f_w >= 175.0) & (f_w <= 185.0)
    measured = 10.0 * np.log10(np.mean(pxx[band]) / np.mean(pyy[band]))
    rec.info("T-LFE-01: BW4 at 120 Hz on white noise: attenuation at 175-185 Hz, measured vs analytic [dB]",
             (round(measured, 2), round(10.0 * np.log10(np.mean(np.abs(svf.freqz(lp("butter", 4, FC), f_w[band], FS)) ** -2)), 2)))
    rec.check("T-LFE-01: 24 dB/oct low-pass attenuates >= 14 dB at 1.5 fc (measured on wideband noise)", measured, ">=", 14.0, "dB")

    # T-LFE-07: displayed group delay (analysis) vs measured from the phase of two sines around 40 Hz
    s = lp("butter", 4, FC)
    t = np.arange(4 * FS) / FS
    ph = {}
    for f in (39.5, 40.5):
        yy = svf.process_fast(s, np.sin(2 * np.pi * f * t))[2 * FS:]
        tt = t[2 * FS:]
        a, b = np.linalg.lstsq(np.stack([np.cos(2 * np.pi * f * tt), np.sin(2 * np.pi * f * tt)], 1), yy, rcond=None)[0]
        ph[f] = np.arctan2(-b, a)  # y = a cos + b sin = A cos(wt + phi), phi = atan2(-b, a)
    measured_gd = -(ph[40.5] - ph[39.5]) / (2 * np.pi * 1.0)
    shown_gd = float(svf.group_delay_s(s, [40.0], FS)[0])
    rec.info("T-LFE-07: BW4 120 Hz group delay at 40 Hz, displayed vs measured [ms]", (round(1000 * shown_gd, 4), round(1000 * measured_gd, 4)))
    rec.check("T-LFE-07: displayed vs measured group delay at 40 Hz", abs(measured_gd / shown_gd - 1.0) * 100.0, "<=", 2.0, "%")

    # ---------------------------------------------------------------- 2. cascade with the encoder filter
    sos = lfe.encoder_filter_sos(FS)
    f_grid = np.geomspace(5.0, 400.0, 4000)
    enc_db = 20.0 * np.log10(np.abs(sps.sosfreqz(sos, worN=f_grid, fs=FS)[1]))
    casc = {}
    for name in ("BW12", "BW24", "BW48", "LR24", "Bessel24"):
        kind, order = opts[name]
        for fc in (80.0, 100.0, 120.0):
            s = lp(kind, order, fc)
            tot = mag_db(s, f_grid) + enc_db
            f3 = float(f_grid[np.argmax(tot < -3.0)])
            fq = np.array([40.0, 80.0, 120.0])
            gd_l, gd_e = svf.group_delay_s(s, fq, FS), lfe.group_delay_sos_s(sos, fq, FS)
            casc[f"{name} @ {fc:g} Hz + encoder"] = {
                "gain at 40/60/80/100/120 Hz [dB]": tuple(round(float(v), 2) for v in np.interp([40, 60, 80, 100, 120], f_grid, tot)),
                "ripple 20-80 Hz [dB]": round(float(np.ptp(tot[(f_grid >= 20) & (f_grid <= 80)])), 2),
                "-3 dB at [Hz]": round(f3, 1),
                "group delay LFE LP + encoder = total at 40/80/120 Hz [ms]": tuple(round(float(1000 * (a + b)), 2) for a, b in zip(gd_l, gd_e))}
    rec.info("plug-in LFE low-pass cascaded with the A/52-style encoder low-pass (ellip 8, assumed 0.1 dB / 80 dB at 120 Hz)", casc)
    rec.info("encoder low-pass alone: gain at 100/120/125/130/140/150/200 Hz [dB]",
             tuple(round(float(v), 2) for v in np.interp([100, 120, 125, 130, 140, 150, 200], f_grid, enc_db[:len(f_grid)])) if enc_db.min() > -300 else "n/a")

    # recommendation: >= 14 dB at 1.5 fc (T-LFE-01 example), at most 1.5 dB down at 80 Hz, then the smallest delay at 40 Hz
    cands = {}
    for name, (kind, order) in opts.items():
        s = lp(kind, order, FC)
        if lfe.out_of_band_db(kind, order, FC, FS) >= 14.0 and mag_db(s, 80.0)[0] >= -1.5:
            cands[name] = 1000.0 * float(svf.group_delay_s(s, [40.0], FS)[0])
    best = min(cands, key=cands.get)
    rec.info("slopes meeting >= 14 dB at 1.5 fc and >= -1.5 dB at 80 Hz (fc 120): group delay at 40 Hz [ms]", {k: round(v, 2) for k, v in cands.items()})
    rec.info("recommended default LFE low-pass", f"{best} at 120 Hz (single filter, BS.775: no cascade of one's own LPFs; the encoder's comes on top)")

    # ---------------------------------------------------------------- 3. linear-phase FIR
    fir = {}
    for fs in (44100, 48000, 96000, 192000):
        row = {}
        for beta in (lfe.DEFAULT_BETA, 8.0):
            n = lfe.min_taps(target(fs), fs, beta=beta)
            row[f"beta {beta:g}"] = {"min taps": n, "kernel [ms]": round(1000.0 * n / fs, 1), "latency (N-1)/2 [ms]": round(1000.0 * (n - 1) / 2.0 / fs, 1)}
        fir[fs] = row
    rec.info("HP 20 Hz + LP 120 Hz (BW4 each): shortest kernel within 0.1 dB over 25-2000 Hz", fir)
    n48 = fir[48000][f"beta {lfe.DEFAULT_BETA:g}"]["min taps"]
    rec.check("linear phase HP 20 + LP 120 at 48 kHz fits the SPEC's 16384 taps (beta 3)", n48, "<=", 16384)
    rec.info("T-FILT-06 with the SPEC 0.1 beta 8 at 16384 taps, 48 kHz: amplitude error [dB] (SPEC-literal; beta 3 since SPEC 0.2)",
             round(lfe.fir_error_db(lfe.linear_phase_fir(target(FS), FS, 16384, beta=8.0), target(FS), FS), 3))
    rec.check("T-FILT-06 with beta 3 at 16384 taps, 48 kHz: amplitude error",
              lfe.fir_error_db(lfe.linear_phase_fir(target(FS), FS, 16384), target(FS), FS), "<=", 0.1, "dB")
    rec.info("SPEC limit of 32768 taps at 192 kHz (beta 3 needs)", fir[192000][f"beta {lfe.DEFAULT_BETA:g}"]["min taps"])

    corners = {}
    for hp in (20.0, 30.0, 40.0, 60.0):
        n = lfe.min_taps(target(FS, hp=hp), FS, f_check=(1.25 * hp, 2000.0))
        corners[f"HP {hp:g} Hz + LP 120"] = {"min taps": n, "latency [ms]": round(1000.0 * (n - 1) / 2.0 / FS, 1)}
    n = lfe.min_taps(target(FS, hp=None), FS, f_check=(25.0, 2000.0))
    corners["LP 120 Hz only"] = {"min taps": n, "latency [ms]": round(1000.0 * (n - 1) / 2.0 / FS, 1)}
    rec.info("48 kHz, beta 3: the cost of the high-pass corner (kernel for 0.1 dB over 1.25 fc ... 2 kHz)", corners)
    spec_rule = {}
    for fs in (44100, 48000, 96000, 192000):
        n_rule = min(32768, 2 ** int(np.ceil(np.log2(6.0 * fs / 20.0))))
        spec_rule[fs] = {"SPEC N = 2^k >= 6 fs / 20 Hz (limit 32768)": n_rule,
                         "error beta 8 [dB]": round(lfe.fir_error_db(lfe.linear_phase_fir(target(fs), fs, n_rule, beta=8.0), target(fs), fs), 3),
                         "error beta 3 [dB]": round(lfe.fir_error_db(lfe.linear_phase_fir(target(fs), fs, n_rule), target(fs), fs), 3)}
    rec.info("the SPEC's length rule for a 20 Hz corner: amplitude error 25-2000 Hz", spec_rule)

    # 3b. Linear Phase for the low-pass only (D5, SPEC 0.2 §7.6; D8 M3): a fixed kernel N_LIN per sample rate, so the
    #     latency does not depend on sub_lp_freq. Accuracy: 0.1 dB from 5 Hz down to the -60 dB point of the target.
    def n_lin(fs):
        return 4096 * 2 ** int(np.ceil(np.log2(fs / 48000.0) - 1e-12)) + 1

    def lp_only(fam, order, fc, fs):
        secs = {"BW": svf.design_butter, "LR": svf.design_lr, "Bessel": svf.design_bessel}[fam](order, fc, fs, "low")
        return lambda f: np.maximum(np.abs(svf.freqz(secs, f, fs)), 1e-15)

    def band(tgt, fc, fs):
        grid = np.geomspace(fc, fs / 2.5, 4000)
        return 5.0, float(grid[np.argmax(20.0 * np.log10(tgt(grid)) <= -60.0)])

    designs = [("BW", 2), ("BW", 4), ("BW", 8), ("BW", 16), ("LR", 4), ("LR", 8), ("LR", 16), ("Bessel", 2), ("Bessel", 4), ("Bessel", 8)]
    names = {("BW", 2): "BW 12", ("BW", 4): "BW 24", ("BW", 8): "BW 48", ("BW", 16): "BW 96", ("LR", 4): "LR 24",
             ("LR", 8): "LR 48", ("LR", 16): "LR 96", ("Bessel", 2): "Bessel 12", ("Bessel", 4): "Bessel 24", ("Bessel", 8): "Bessel 48"}
    lp_taps, lp_err = {}, {}
    for fam, order in designs:
        taps_row, err_row = {}, {}
        for fc in (30.0, 60.0, 80.0, 120.0, 140.0):
            tgt = lp_only(fam, order, fc, FS)
            fb = band(tgt, fc, FS)
            taps_row[f"{fc:g} Hz"] = lfe.min_taps(tgt, FS, f_check=fb, n_max=1 << 19)
            err_row[f"{fc:g} Hz"] = round(lfe.fir_error_db(lfe.linear_phase_fir(tgt, FS, n_lin(FS)), tgt, FS, fb), 2)
        lp_taps[names[(fam, order)]], lp_err[names[(fam, order)]] = taps_row, err_row
    rec.info("LP only, 48 kHz: shortest kernel for 0.1 dB (5 Hz .. -60 dB point) by design and corner", lp_taps)
    rec.info(f"LP only, 48 kHz: error at N_LIN = {n_lin(FS)} taps [dB] by design and corner", lp_err)
    lat = {fs: {"N_LIN": n_lin(fs), "(N_LIN - 1)/2 [ms]": round(1000.0 * (n_lin(fs) - 1) / 2.0 / fs, 1),
                "+ 256-sample partition [ms]": round(1000.0 * ((n_lin(fs) - 1) / 2.0 + 256) / fs, 1)} for fs in (44100, 48000, 88200, 96000, 176400, 192000)}
    rec.info("N_LIN = 4096 * 2^ceil(log2(fs / 48 kHz)) + 1 and its latency", lat)
    defaults = {}
    for fs in (44100, 48000, 96000, 192000):
        for label, fc in (("LFE LP BW 24 @ 120 Hz", 120.0), ("sub LP BW 24 @ 140 Hz", 140.0)):
            tgt = lp_only("BW", 4, fc, fs)
            defaults[f"{fs} | {label}"] = lfe.fir_error_db(lfe.linear_phase_fir(tgt, fs, n_lin(fs)), tgt, fs, band(tgt, fc, fs))
    rec.info("default low-passes at N_LIN: error [dB]", {k: round(v, 3) for k, v in defaults.items()})
    rec.check("Linear Phase LP at N_LIN: the default low-passes (BW 24 at 120 / 140 Hz) within 0.1 dB, all fs",
              max(defaults.values()), "<=", 0.1, "dB")

    # |H|^2 against |H|
    t_amp = target(FS)
    f_pts = np.array([15.0, 20.0, 120.0, 180.0])
    h_amp = lfe.linear_phase_fir(t_amp, FS, 16384)
    h_pow = lfe.linear_phase_fir(lambda f: t_amp(f) ** 2, FS, 16384)
    resp = lambda h: 20.0 * np.log10(np.abs(np.fft.rfft(h, 2 ** 19))[np.round(f_pts / FS * 2 ** 19).astype(int)])
    rec.info("amplitude at 15/20/120/180 Hz [dB]: prototype |H| | FIR of |H| | FIR of |H|^2 (= forward-backward filtering)",
             {"prototype": np.round(20 * np.log10(t_amp(f_pts)), 2), "FIR |H|": np.round(resp(h_amp), 2), "FIR |H|^2": np.round(resp(h_pow), 2)})
    rec.info("correction", "the Linear Phase kernel must follow the prototype's amplitude |H| (corner -3 dB), not |H|^2 (-6 dB, doubled slope)")

    # ---------------------------------------------------------------- plots
    fig, ax = plt.subplots(2, 3, figsize=(16, 8.5))
    fg = np.geomspace(10.0, 600.0, 600)
    for name in ("BW12", "BW24", "BW48", "BW96", "LR24", "Bessel24"):
        kind, order = opts[name]
        ax[0, 0].semilogx(fg, mag_db(lp(kind, order, FC), fg), label=name)
        ax[0, 1].semilogx(fg, 1000.0 * svf.group_delay_s(lp(kind, order, FC), fg, FS), label=name)
    ax[0, 0].set(xlabel="Hz", ylabel="dB", ylim=(-70, 3), title="LFE low-pass at 120 Hz")
    ax[0, 1].set(xlabel="Hz", ylabel="group delay [ms]", ylim=(0, 12), title="Group delay")
    for a in ax[0, :2]:
        a.legend(fontsize=7); a.grid(alpha=.3, which="both")
    for fc in (80.0, 100.0, 120.0):
        ax[0, 2].semilogx(f_grid, mag_db(lp("butter", 4, fc), f_grid) + enc_db, label=f"BW24 @ {fc:g} Hz + encoder")
    ax[0, 2].semilogx(f_grid, enc_db, "k--", lw=0.8, label="encoder alone")
    ax[0, 2].set(xlabel="Hz", ylabel="dB", ylim=(-40, 3), xlim=(10, 400), title="Cascade with the encoder low-pass")
    ax[0, 2].legend(fontsize=7); ax[0, 2].grid(alpha=.3, which="both")
    ns = [1024, 2048, 4096, 8192, 16384, 32768]
    for beta in (3.0, 8.0):
        ax[1, 0].loglog(ns, [lfe.fir_error_db(lfe.linear_phase_fir(target(FS), FS, n, beta=beta), target(FS), FS) for n in ns], "o-", label=f"beta {beta:g}")
    ax[1, 0].axhline(0.1, color="gray", lw=0.6)
    ax[1, 0].set(xlabel="taps", ylabel="max error 25-2000 Hz [dB]", title="HP 20 + LP 120 at 48 kHz: FIR accuracy")
    ax[1, 0].legend(fontsize=7); ax[1, 0].grid(alpha=.3, which="both")
    hps = [20.0, 30.0, 40.0, 60.0]
    ax[1, 1].plot(hps, [corners[f"HP {h:g} Hz + LP 120"]["latency [ms]"] for h in hps], "o-")
    ax[1, 1].axhline(corners["LP 120 Hz only"]["latency [ms]"], color="gray", ls="--", label="LP 120 only")
    ax[1, 1].set(xlabel="high-pass corner [Hz]", ylabel="linear-phase latency [ms]", title="Latency for 0.1 dB at 48 kHz (beta 3)")
    ax[1, 1].legend(fontsize=7); ax[1, 1].grid(alpha=.3)
    ff = np.geomspace(8.0, 400.0, 500)
    ax[1, 2].semilogx(ff, 20 * np.log10(t_amp(ff)), "k", label="prototype |H|")
    ax[1, 2].semilogx(ff, 20 * np.log10(np.abs(np.fft.rfft(h_amp, 2 ** 19))[np.round(ff / FS * 2 ** 19).astype(int)]), "--", label="FIR of |H|")
    ax[1, 2].semilogx(ff, 20 * np.log10(np.abs(np.fft.rfft(h_pow, 2 ** 19))[np.round(ff / FS * 2 ** 19).astype(int)]), ":", label="FIR of |H|^2")
    ax[1, 2].set(xlabel="Hz", ylabel="dB", ylim=(-40, 3), title="Target |H| vs |H|^2")
    ax[1, 2].legend(fontsize=7); ax[1, 2].grid(alpha=.3, which="both")
    fig.tight_layout()
    fig.savefig(rec.fig_path("lfe"), dpi=100)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
