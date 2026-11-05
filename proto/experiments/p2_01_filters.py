# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-01: TPT filters (Butterworth / Linkwitz-Riley / Bessel) and Cytomic shapes vs SciPy and RBJ."""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy import signal as sps

from experiments._common import Recorder
from subgen_proto import svf

FS_ALL = (44100, 48000, 88200, 96000, 176400, 192000)
FC_ALL = (15.0, 22.0, 30.0, 60.0, 120.0, 500.0)


def _db(h):
    return 20.0 * np.log10(np.abs(h) + 1e-300)


def _err(ours, ref, floor_db=-150.0):
    a, b = _db(ours), _db(ref)
    return float(np.max(np.abs(a - b)[b > floor_db]))


def _rbj(kind, f0, q, gain_db, fs):
    a_lin = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * f0 / fs
    cw, alpha = np.cos(w0), np.sin(w0) / (2.0 * q)
    s = 2.0 * np.sqrt(a_lin) * alpha
    if kind == "bell":
        return (np.array([1 + alpha * a_lin, -2 * cw, 1 - alpha * a_lin]),
                np.array([1 + alpha / a_lin, -2 * cw, 1 - alpha / a_lin]))
    if kind == "lowshelf":
        return (np.array([a_lin * ((a_lin + 1) - (a_lin - 1) * cw + s), 2 * a_lin * ((a_lin - 1) - (a_lin + 1) * cw),
                          a_lin * ((a_lin + 1) - (a_lin - 1) * cw - s)]),
                np.array([(a_lin + 1) + (a_lin - 1) * cw + s, -2 * ((a_lin - 1) + (a_lin + 1) * cw),
                          (a_lin + 1) + (a_lin - 1) * cw - s]))
    return (np.array([a_lin * ((a_lin + 1) + (a_lin - 1) * cw + s), -2 * a_lin * ((a_lin - 1) + (a_lin + 1) * cw),
                      a_lin * ((a_lin + 1) + (a_lin - 1) * cw - s)]),
            np.array([(a_lin + 1) - (a_lin - 1) * cw + s, 2 * ((a_lin - 1) - (a_lin + 1) * cw),
                      (a_lin + 1) - (a_lin - 1) * cw - s]))


def main():
    rec = Recorder("p2_01")

    # --- Butterworth and Linkwitz-Riley vs SciPy, all fs, all fc, orders 2..16
    worst = {}
    for fs in FS_ALL:
        f = np.geomspace(5.0, 0.45 * fs, 240)
        for order in (2, 4, 8, 16):
            for fc in FC_ALL:
                for kind in ("low", "high"):
                    sos = sps.butter(order, fc, btype=kind, fs=fs, output="sos")
                    _, ref = sps.sosfreqz(sos, worN=f, fs=fs)
                    worst["BW", fs] = max(worst.get(("BW", fs), 0.0),
                                          _err(svf.freqz(svf.design_butter(order, fc, fs, kind), f, fs), ref))
                    if order <= 8:
                        sos2 = sps.butter(order, fc, btype=kind, fs=fs, output="sos")
                        _, ref2 = sps.sosfreqz(sos2, worN=f, fs=fs)
                        ours = svf.freqz(svf.design_lr(2 * order, fc, fs, kind), f, fs)
                        worst["LR", fs] = max(worst.get(("LR", fs), 0.0), _err(ours, ref2 * ref2))
    for (name, fs), v in sorted(worst.items()):
        rec.info(f"{name} max dB error vs SciPy @ {fs} Hz", round(v, 8))
    rec.check("Butterworth worst error vs SciPy (6 fs x 6 fc x orders 2-16)",
              max(v for (n, _), v in worst.items() if n == "BW"), "<=", 0.01, "dB")
    rec.check("Linkwitz-Riley worst error vs SciPy (LRn = BW(n/2)^2)",
              max(v for (n, _), v in worst.items() if n == "LR"), "<=", 0.01, "dB")

    # --- Bessel vs SciPy
    wb = 0.0
    for fs in FS_ALL:
        f = np.geomspace(5.0, 0.45 * fs, 240)
        for order in (2, 4, 8):
            for fc in FC_ALL:
                for kind in ("low", "high"):
                    sos = sps.bessel(order, fc, btype=kind, fs=fs, norm="mag", output="sos")
                    _, ref = sps.sosfreqz(sos, worN=f, fs=fs)
                    wb = max(wb, _err(svf.freqz(svf.design_bessel(order, fc, fs, kind), f, fs), ref))
    rec.check("Bessel worst error vs SciPy (norm='mag')", wb, "<=", 0.01, "dB")

    # --- Cytomic vs RBJ
    wc = {"bell": 0.0, "lowshelf": 0.0, "highshelf": 0.0}
    for fs in FS_ALL:
        f = np.geomspace(10.0, 0.45 * fs, 240)
        for kind in wc:
            for gain in (-18.0, -6.0, 6.0, 18.0):
                for q in (0.3, 0.71, 1.0, 4.0, 10.0):
                    for f0 in (15.0, 40.0, 160.0, 1000.0, 5000.0):
                        if f0 >= 0.4 * fs:
                            continue
                        b, a = _rbj(kind, f0, q, gain, fs)
                        _, ref = sps.freqz(b, a, worN=f, fs=fs)
                        wc[kind] = max(wc[kind], _err(svf.freqz([svf.cytomic(kind, f0, q, gain, fs)], f, fs), ref))
    for kind, v in wc.items():
        rec.check(f"Cytomic {kind} worst error vs RBJ (6 fs)", v, "<=", 0.05, "dB")

    # --- time domain at the hardest corner: 15 Hz at 192 kHz, 4th-order low-pass, 2^18 samples
    fs, n = 192000, 2 ** 18
    secs = svf.design_butter(4, 15.0, fs, "low")
    imp = np.zeros(n)
    imp[0] = 1.0
    h = np.fft.rfft(svf.process(secs, imp))
    f = np.fft.rfftfreq(n, 1.0 / fs)
    sel = (f >= 1.0) & (f <= 100.0)
    ref = svf.freqz(secs, f[sel], fs)
    rec.check("TPT loop vs analytic response, 15 Hz LP4 @ 192 kHz (max relative error 1-100 Hz)",
              float(np.max(np.abs(h[sel] - ref) / np.abs(ref))), "<=", 1e-6)

    # --- why double precision: float32-quantised biquad coefficients of the same filter
    sos = svf.to_sos(secs)
    sos32 = sos.astype(np.float32).astype(np.float64)
    fl = np.geomspace(2.0, 200.0, 200)
    _, h64 = sps.sosfreqz(sos, worN=fl, fs=fs)
    _, h32 = sps.sosfreqz(sos32, worN=fl, fs=fs)
    err32 = float(np.max(np.abs(_db(h32) - _db(h64))))
    rec.info("float32-quantised coefficients, 15 Hz LP4 @ 192 kHz: max response error 2-200 Hz (dB)", round(err32, 3))
    rec.check("float32 coefficients are NOT adequate at 15 Hz / 192 kHz (justifies double)", err32, ">", 0.05, "dB")

    # --- plot: 120 Hz low-pass, magnitude and group delay for BW / LR / Bessel
    fs = 48000
    fp = np.geomspace(5.0, 1000.0, 400)
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))
    for label, secs in (("Butterworth 4", svf.design_butter(4, 120.0, fs, "low")),
                        ("Linkwitz-Riley 4", svf.design_lr(4, 120.0, fs, "low")),
                        ("Bessel 4", svf.design_bessel(4, 120.0, fs, "low")),
                        ("Butterworth 8", svf.design_butter(8, 120.0, fs, "low")),
                        ("Bessel 8", svf.design_bessel(8, 120.0, fs, "low"))):
        ax[0].semilogx(fp, _db(svf.freqz(secs, fp, fs)), label=label)
        ax[1].semilogx(fp, 1000.0 * svf.group_delay_s(secs, fp, fs), label=label)
    ax[0].set(xlabel="Hz", ylabel="dB", ylim=(-60, 3), title="Low-pass 120 Hz, fs 48 kHz")
    ax[1].set(xlabel="Hz", ylabel="ms", title="Group delay")
    ax[0].grid(True, which="both", alpha=0.3)
    ax[1].grid(True, which="both", alpha=0.3)
    ax[0].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(rec.fig_path("responses"), dpi=110)
    plt.close(fig)

    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
