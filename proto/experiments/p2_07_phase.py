# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-07: all-pass Hilbert pair, phase rotation and Auto-Align."""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy import signal as sps

from experiments._common import Recorder
from subgen_proto import align, band, divide, hilbert, signals, trigger

FS = 48000


def wrap(a):
    return (a + 180.0) % 360.0 - 180.0


def comp(y, fs, f, seconds=1.0):
    n = int(seconds * fs)
    z = np.fft.rfft(y[-n:])[int(round(f * n / fs))]
    return float(np.degrees(np.angle(z))), float(2.0 * abs(z) / n)


def group_delay_ms(d, f, poles):
    w = 2.0 * np.pi * np.asarray(f)[:, None]
    return 1000.0 * np.sum(2.0 * poles / (poles ** 2 + w ** 2), axis=1)


def main():
    rec = Recorder("p2_07")

    # ---------------------------------------------------------------- 1. order vs error
    tab = {}
    for n in range(1, 8):
        d = hilbert.design(n)
        eps = np.radians(d.max_err_deg)
        tab[n] = {"max phase error [deg]": round(d.max_err_deg, 4), "first-order sections (A+B)": 2 * n,
                  "rotation amplitude error at 45 deg [dB]": round(8.686 * eps / 2.0, 4)}
    rec.info("12-600 Hz pair: sections per chain -> error", tab)
    n1, n05, n01 = (hilbert.min_sections(e) for e in (1.0, 0.5, 0.1))
    rec.info("min sections per chain for 1 / 0.5 / 0.1 degrees", (n1, n05, n01))
    rec.check("min_sections(1 deg) per chain", n1, "<=", 10)
    rec.info("rotation amplitude |y| = sqrt(1 - sin(2 theta) sin(eps)): +-0.05 dB needs eps <= 0.66 deg; a 1 deg pair gives", "+-0.076 dB at theta = 45 deg")
    rec.info("recommended order", f"{n05} sections per chain ({2 * n05} first-order all-passes, error {tab[n05]['max phase error [deg]']} deg)")
    for lo, hi in ((20.0, 300.0), (30.0, 300.0), (12.0, 1000.0)):
        rec.info(f"band {lo:g}-{hi:g} Hz: min sections for 1 / 0.5 deg", (hilbert.min_sections(1.0, lo, hi), hilbert.min_sections(0.5, lo, hi)))

    # ---------------------------------------------------------------- 2. group delay
    d = hilbert.design(n05)
    fq = np.array([20.0, 30.0, 50.0, 80.0, 120.0, 200.0, 300.0])
    gd_a, gd_b = group_delay_ms(d, fq, d.poles_a), group_delay_ms(d, fq, d.poles_b)
    rec.info(f"group delay of chain A / B [ms] at {fq.tolist()} Hz (n = {n05})",
             {"A": np.round(gd_a, 2), "B": np.round(gd_b, 2)})
    rec.info("sub path: rotation stage at theta = 0 delays the sub by chain A's group delay (ms) at 30 / 50 / 100 Hz",
             tuple(round(float(v), 2) for v in np.interp([30.0, 50.0, 100.0], fq, gd_a)))

    alt = {}
    for lo, hi in ((12.0, 600.0), (20.0, 600.0), (20.0, 300.0), (30.0, 300.0)):
        da = hilbert.design(hilbert.min_sections(0.5, lo, hi), lo, hi)
        alt[f"{lo:g}-{hi:g} Hz"] = {"sections/chain": len(da.poles_a), "A group delay at 30/50/100 Hz [ms]":
                                    tuple(round(float(v), 2) for v in group_delay_ms(da, np.array([30.0, 50.0, 100.0]), da.poles_a))}
    rec.info("design band vs the group delay it costs (0.5 degree designs)", alt)
    ph_a = lambda f_: float(np.degrees(-2.0 * np.sum(np.arctan(2.0 * np.pi * f_ / d.poles_a))))
    rec.info("phase of chain A (the rotation stage at theta = 0) at 30 / 45 / 80 / 200 Hz [deg, wrapped]", tuple(round(float(wrap(ph_a(f_))), 1) for f_ in (30.0, 45.0, 80.0, 200.0)))

    # ---------------------------------------------------------------- 3. rotation accuracy (T-PHS-02)
    worst_p, worst_a, per_fs = 0.0, 0.0, {}
    thetas = np.arange(-180.0, 181.0, 15.0)
    for fs in (44100, 48000, 96000):
        ep, ea = 0.0, 0.0
        for f in (30.0, 50.0, 100.0, 200.0, 300.0):
            x = signals.sine(f, fs, 3.0, level_db=-6.0)
            p0, a0 = comp(hilbert.rotate(x, 0.0, d, fs), fs, f)
            for th in thetas:
                p, a = comp(hilbert.rotate(x, th, d, fs), fs, f)
                ep = max(ep, abs(wrap(p - p0 - th)))
                ea = max(ea, abs(20 * np.log10(a / a0)))
        per_fs[fs] = (round(ep, 3), round(ea, 4))
        worst_p, worst_a = max(worst_p, ep), max(worst_a, ea)
    rec.info("rotation error by sample rate: (max phase [deg], max amplitude [dB]) over theta -180..180 step 15, f 30-300 Hz", per_fs)
    rec.check("T-PHS-02: rotation phase error", worst_p, "<=", 1.0, "deg")
    rec.check("T-PHS-02: rotation amplitude error", worst_a, "<=", 0.05, "dB")
    d3 = hilbert.design(3)
    ea3 = max(abs(20 * np.log10(comp(hilbert.rotate(signals.sine(f, FS, 3.0, -6.0), th, d3, FS), FS, f)[1] /
                                comp(hilbert.rotate(signals.sine(f, FS, 3.0, -6.0), 0.0, d3, FS), FS, f)[1]))
              for f in (30.0, 100.0, 300.0) for th in (-135.0, -45.0, 45.0, 135.0))
    rec.info("the 3-section pair (0.86 deg, enough for the 1 deg criterion) rotates with an amplitude error of [dB]", round(ea3, 3))
    x = np.random.default_rng(0).standard_normal(1000)
    rec.check("T-PHS-01: polarity, x + (-x) == 0", float(np.max(np.abs(x + hilbert.polarity(x, -1)))), "==", 0.0)

    # ---------------------------------------------------------------- 4. Auto-Align: synthetic scenarios with a known optimum
    def scenario(phi_d, f=80.0, dur=2.0):
        t = np.arange(int(dur * FS)) / FS
        return (np.sin(2 * np.pi * f * t) + 0.5 * np.sin(2 * np.pi * f / 2 * t + np.radians(phi_d)),
                np.sin(2 * np.pi * f / 2 * t))

    errs, confs = [], []
    for phi in np.arange(-180.0, 180.0, 7.5):  # half-way between the 15 degree grid points as well
        r = align.auto_align(*scenario(phi), FS, (20.0, 120.0))
        errs.append(abs(wrap(r.rot_deg + (180.0 if r.pol < 0 else 0.0) - phi)))
        confs.append(r.confidence_db)
    rec.info("known optimum: worst error [deg] / lowest confidence [dB] over 48 optima (0, 7.5, 15 ... degrees)", (max(errs), round(min(confs), 2)))
    rec.check("T-PHS-03: Auto-Align within 15 degrees of the optimum (48 synthetic optima)", max(errs), "<=", 15.0, "deg")
    t = np.arange(2 * FS) / FS
    none = align.auto_align(np.sin(2 * np.pi * 80 * t), np.sin(2 * np.pi * 40 * t), FS, (20.0, 120.0))
    rec.check("T-PHS-03: no overlap -> low confidence", none.confidence_db, "<", 0.5, "dB")

    # ---------------------------------------------------------------- 5. Auto-Align on SIG-18 (kick + 808)
    x = signals.kick_plus_808(FS)
    dry = band.range_filter(x, FS, 15.0, 120.0)   # S1 through [range_lo/2, range_hi]
    xb = band.range_filter(x, FS, 30.0, 120.0)    # what the engines see
    res = divide.divide(xb, FS)
    subs = {"DIVIDE Sub -1 (smooth, gated)": divide.apply_confidence_gate(
                divide.smooth_sub(res, FS, 1), res.conf, 50.0, FS, res.provisional, divide.GATE_CONFIRM,
                divide.confirmations(res.conf, res.provisional, 50.0)),
            "TRIGGER (80 -> 45 Hz)": trigger.render(xb, FS)}
    sig18, curves = {}, {}
    psi_of = lambda q: wrap(q.rot_deg + (180.0 if q.pol < 0 else 0.0))
    for name, sub in subs.items():
        sub_a = hilbert.allpass_chain(sub, d.poles_a, FS)  # what the rotation stage actually rotates (theta = 0 is chain A)
        for obj in ("energy", "headroom"):
            r = align.auto_align(dry, sub, FS, (20.0, 120.0), objective=obj)
            fine = align.auto_align(dry, sub, FS, (20.0, 120.0), objective=obj, step_deg=1.0)
            after = align.auto_align(dry, sub_a, FS, (20.0, 120.0), objective=obj)
            sig18[f"{name} | {obj}"] = {
                "pol": r.pol, "rot [deg]": r.rot_deg, "gain [dB]": round(r.gain_db, 2), "confidence [dB]": round(r.confidence_db, 2),
                "1 deg optimum: psi [deg] / gain [dB]": (psi_of(fine), round(fine.gain_db, 2)),
                "15 deg grid vs 1 deg: angle [deg] / gain lost [dB]": (round(abs(wrap(psi_of(r) - psi_of(fine))), 1), round(fine.gain_db - r.gain_db, 2)),
                "sub taken AFTER chain A: psi [deg] (before: %.0f)" % psi_of(r): psi_of(after)}
        a_ = sps.hilbert(sub)
        psis = np.arange(0.0, 360.0, 5.0)
        w = np.hanning(len(dry))
        keep = (np.fft.rfftfreq(len(dry), 1.0 / FS) >= 20) & (np.fft.rfftfreq(len(dry), 1.0 / FS) <= 120)
        curves[name] = (psis,
                        [10 * np.log10(np.sum(np.abs(np.fft.rfft(w * (dry + np.real(a_ * np.exp(1j * np.radians(p)))))[keep]) ** 2)) for p in psis],
                        [20 * np.log10(np.max(np.abs(dry + np.real(a_ * np.exp(1j * np.radians(p)))))) for p in psis])
    rec.info("SIG-18 (kick + 808): Auto-Align, band 20-120 Hz", sig18)
    lost = max(v["15 deg grid vs 1 deg: angle [deg] / gain lost [dB]"][1] for v in sig18.values())
    rec.info("SIG-18: worst angle between the 15 degree result and the 1 degree optimum [deg]",
             max(v["15 deg grid vs 1 deg: angle [deg] / gain lost [dB]"][0] for v in sig18.values()))
    rec.check("SIG-18: the 15 degree grid loses at most 0.5 dB against the 1 degree optimum (all 4 cases)", lost, "<=", 0.5, "dB")

    # ---------------------------------------------------------------- plots
    fig, ax = plt.subplots(2, 3, figsize=(16, 8.5))
    f = np.geomspace(3.0, 2000.0, 600)
    for n in (2, 3, 4, 5):
        ax[0, 0].semilogx(f, hilbert.phase_difference_deg(hilbert.design(n), f), label=f"n = {n}")
    ax[0, 0].axvspan(12, 600, color="gray", alpha=0.12)
    ax[0, 0].set(xlabel="Hz", ylabel="phase(B) - phase(A) [deg]", ylim=(60, 120), title="Hilbert pair: shaded = design band 12-600 Hz")
    ax[0, 0].legend(fontsize=7); ax[0, 0].grid(alpha=.3, which="both")
    ff = np.geomspace(12, 600, 400)
    for n in (2, 3, 4, 5):
        ax[0, 1].semilogx(ff, hilbert.phase_difference_deg(hilbert.design(n), ff) - 90.0, label=f"n = {n}")
    ax[0, 1].axhline(1, color="gray", lw=0.6); ax[0, 1].axhline(-1, color="gray", lw=0.6)
    ax[0, 1].set(xlabel="Hz", ylabel="error [deg]", ylim=(-3, 3), title="Equiripple phase error in the band")
    ax[0, 1].legend(fontsize=7); ax[0, 1].grid(alpha=.3, which="both")
    fg = np.geomspace(5, 600, 300)
    ax[0, 2].semilogx(fg, group_delay_ms(d, fg, d.poles_a), label="chain A")
    ax[0, 2].semilogx(fg, group_delay_ms(d, fg, d.poles_b), label="chain B")
    ax[0, 2].set(xlabel="Hz", ylabel="group delay [ms]", title=f"Group delay of the pair (n = {n05})")
    ax[0, 2].legend(fontsize=7); ax[0, 2].grid(alpha=.3, which="both")
    th = np.arange(-180, 181, 5.0)
    for nn, dd in ((3, d3), (n05, d)):
        amp = [20 * np.log10(comp(hilbert.rotate(signals.sine(100.0, FS, 3.0, -6.0), t_, dd, FS), FS, 100.0)[1] /
                             comp(hilbert.rotate(signals.sine(100.0, FS, 3.0, -6.0), 0.0, dd, FS), FS, 100.0)[1]) for t_ in th]
        ax[1, 0].plot(th, amp, label=f"n = {nn}")
    ax[1, 0].axhline(0.05, color="gray", lw=0.6); ax[1, 0].axhline(-0.05, color="gray", lw=0.6)
    ax[1, 0].set(xlabel="theta [deg]", ylabel="amplitude [dB]", title="Rotation amplitude at 100 Hz")
    ax[1, 0].legend(fontsize=7); ax[1, 0].grid(alpha=.3)
    for name, (psis, en, pk) in curves.items():
        ax[1, 1].plot(psis, np.array(en) - max(en), label=f"{name}: energy")
    ax[1, 1].set(xlabel="effective rotation psi [deg]", ylabel="band energy re best [dB]", title="Auto-Align energy objective, SIG-18")
    ax[1, 1].legend(fontsize=7); ax[1, 1].grid(alpha=.3)
    for name, (psis, en, pk) in curves.items():
        ax[1, 2].plot(psis, pk, label=f"{name}: peak")
    ax[1, 2].set(xlabel="effective rotation psi [deg]", ylabel="peak of dry + sub [dBFS]", title="Auto-Align headroom objective, SIG-18")
    ax[1, 2].legend(fontsize=7); ax[1, 2].grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(rec.fig_path("phase"), dpi=100)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
