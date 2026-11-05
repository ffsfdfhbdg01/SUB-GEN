# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the TPT/Cytomic SVF cascades. References are SciPy designs and literal RBJ cookbook formulas."""
import unittest

import numpy as np
from scipy import signal as sps

from subgen_proto import svf

FS_ALL = (44100, 48000, 88200, 96000, 176400, 192000)


def _db(h):
    return 20.0 * np.log10(np.abs(h) + 1e-300)


def _max_err_db(ours, ref, floor_db=-150.0):
    """Largest |dB difference|; bins where the reference is below floor_db are numerically meaningless."""
    a, b = _db(ours), _db(ref)
    return float(np.max(np.abs(a - b)[b > floor_db]))


def _rbj(kind, f0, q, gain_db, fs):
    """Audio EQ Cookbook (R. Bristow-Johnson) biquads, Q form: literal formulas."""
    a_lin = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * f0 / fs
    cw = np.cos(w0)
    alpha = np.sin(w0) / (2.0 * q)
    if kind == "bell":
        b = [1 + alpha * a_lin, -2 * cw, 1 - alpha * a_lin]
        a = [1 + alpha / a_lin, -2 * cw, 1 - alpha / a_lin]
    elif kind == "lowshelf":
        s = 2 * np.sqrt(a_lin) * alpha
        b = [a_lin * ((a_lin + 1) - (a_lin - 1) * cw + s), 2 * a_lin * ((a_lin - 1) - (a_lin + 1) * cw),
             a_lin * ((a_lin + 1) - (a_lin - 1) * cw - s)]
        a = [(a_lin + 1) + (a_lin - 1) * cw + s, -2 * ((a_lin - 1) + (a_lin + 1) * cw),
             (a_lin + 1) + (a_lin - 1) * cw - s]
    else:
        s = 2 * np.sqrt(a_lin) * alpha
        b = [a_lin * ((a_lin + 1) + (a_lin - 1) * cw + s), -2 * a_lin * ((a_lin - 1) + (a_lin + 1) * cw),
             a_lin * ((a_lin + 1) + (a_lin - 1) * cw - s)]
        a = [(a_lin + 1) - (a_lin - 1) * cw + s, 2 * ((a_lin - 1) - (a_lin + 1) * cw),
             (a_lin + 1) - (a_lin - 1) * cw - s]
    return np.array(b), np.array(a)


class TestButterworth(unittest.TestCase):
    def test_butter_matches_scipy(self):
        # Breaks if the section Q, the prewarp g = tan(pi*fc/fs) or the LP/HP mapping is wrong.
        worst = 0.0
        for fs in FS_ALL:
            f = np.geomspace(5.0, 0.45 * fs, 200)
            for order in (2, 4, 8, 16):
                for fc in (15.0, 22.0, 60.0, 120.0, 500.0):
                    for kind in ("low", "high"):
                        ours = svf.freqz(svf.design_butter(order, fc, fs, kind), f, fs)
                        _, ref = sps.sosfreqz(sps.butter(order, fc, btype=kind, fs=fs, output="sos"), worN=f, fs=fs)
                        worst = max(worst, _max_err_db(ours, ref))
        self.assertLess(worst, 0.01)

    def test_odd_orders_use_first_order_section(self):
        f = np.geomspace(5.0, 20000.0, 200)
        for order in (1, 3):
            for kind in ("low", "high"):
                ours = svf.freqz(svf.design_butter(order, 60.0, 48000, kind), f, 48000)
                _, ref = sps.sosfreqz(sps.butter(order, 60.0, btype=kind, fs=48000, output="sos"), worN=f, fs=48000)
                self.assertLess(_max_err_db(ours, ref), 0.01)

    def test_butter_minus_3db_at_cutoff(self):
        # |H(fc)| = 1/sqrt(2) for every order, kind and fs (s' = j exactly at the prewarped cutoff).
        for fs in FS_ALL:
            for order in (1, 2, 3, 4, 8, 16):
                for kind in ("low", "high"):
                    h = svf.freqz(svf.design_butter(order, 60.0, fs, kind), np.array([60.0]), fs)[0]
                    self.assertLess(abs(abs(h) - 2 ** -0.5), 1e-9)


class TestLinkwitzRiley(unittest.TestCase):
    def test_lr_crossover_minus_6db(self):
        # LR = two cascaded Butterworth(order/2): |H(fc)| = 0.5 for every LR order (breaks if only one is used).
        for order in (2, 4, 8, 16):
            for kind in ("low", "high"):
                h = svf.freqz(svf.design_lr(order, 120.0, 48000, kind), np.array([120.0]), 48000)[0]
                self.assertLess(abs(abs(h) - 0.5), 1e-9)

    def test_lr4_lp_hp_sum_allpass(self):
        f = np.geomspace(10.0, 5000.0, 300)
        lp = svf.freqz(svf.design_lr(4, 120.0, 48000, "low"), f, 48000)
        hp = svf.freqz(svf.design_lr(4, 120.0, 48000, "high"), f, 48000)
        self.assertLess(np.max(np.abs(_db(lp + hp))), 0.01)


class TestBessel(unittest.TestCase):
    def test_bessel_matches_scipy(self):
        # Breaks if the per-section prewarp is wrong (SciPy warps once at the corner: g_i = w0_i * tan(pi fc/fs)).
        for fs in (48000, 96000, 192000):
            f = np.geomspace(5.0, 0.45 * fs, 200)
            for order in (2, 4, 8):
                for fc in (22.0, 120.0):
                    for kind in ("low", "high"):
                        ours = svf.freqz(svf.design_bessel(order, fc, fs, kind), f, fs)
                        sos = sps.bessel(order, fc, btype=kind, fs=fs, norm="mag", output="sos")
                        _, ref = sps.sosfreqz(sos, worN=f, fs=fs)
                        self.assertLess(_max_err_db(ours, ref), 0.01, msg=f"{kind} order {order} fc {fc} fs {fs}")

    def test_bessel_group_delay_much_flatter_than_butterworth(self):
        # Hand check, order 2 (delay-normalised H = 3/(s^2+3s+3)): delay at half the -3 dB frequency is 0.9798 of
        # the DC delay (2.0 % down). Orders 4 and 8 are flatter still.
        f = np.linspace(1.0, 60.0, 60)  # up to fc/2 for fc = 120 Hz
        for order, limit in ((2, 0.025), (4, 0.01), (8, 0.01)):
            gd_b = svf.group_delay_s(svf.design_bessel(order, 120.0, 48000, "low"), f, 48000)
            gd_w = svf.group_delay_s(svf.design_butter(order, 120.0, 48000, "low"), f, 48000)
            var_b = (gd_b.max() - gd_b.min()) / gd_b[0]
            var_w = (gd_w.max() - gd_w.min()) / gd_w[0]
            self.assertLess(var_b, limit, msg=f"order {order}")
            self.assertLess(var_b, 0.2 * var_w, msg=f"order {order}")


class TestCytomic(unittest.TestCase):
    def test_cytomic_bell_matches_rbj(self):
        f = np.geomspace(10.0, 20000.0, 300)
        for gain in (-18.0, -6.0, 6.0, 18.0):
            for q in (0.3, 1.0, 10.0):
                for f0 in (40.0, 160.0, 1000.0):
                    ours = svf.freqz([svf.cytomic("bell", f0, q, gain, 48000)], f, 48000)
                    b, a = _rbj("bell", f0, q, gain, 48000)
                    _, ref = sps.freqz(b, a, worN=f, fs=48000)
                    self.assertLess(_max_err_db(ours, ref), 0.05, msg=f"bell {gain} dB Q {q} f {f0}")

    def test_cytomic_shelves_match_rbj(self):
        f = np.geomspace(10.0, 20000.0, 300)
        for kind in ("lowshelf", "highshelf"):
            for gain in (-18.0, -6.0, 6.0, 18.0):
                for q in (0.5, 0.71, 2.0):
                    for f0 in (40.0, 160.0, 1000.0):
                        ours = svf.freqz([svf.cytomic(kind, f0, q, gain, 48000)], f, 48000)
                        b, a = _rbj(kind, f0, q, gain, 48000)
                        _, ref = sps.freqz(b, a, worN=f, fs=48000)
                        self.assertLess(_max_err_db(ours, ref), 0.05, msg=f"{kind} {gain} dB Q {q} f {f0}")

    def test_cytomic_zero_gain_identity(self):
        x = np.random.default_rng(3).standard_normal(2000)
        for kind in ("bell", "lowshelf", "highshelf"):
            y = svf.process([svf.cytomic(kind, 160.0, 1.0, 0.0, 48000)], x)
            self.assertLess(np.max(np.abs(y - x)), 1e-12, msg=kind)


class TestProcessing(unittest.TestCase):
    def test_time_domain_matches_analytic(self):
        # The sample loop (TPT state update) must realise exactly the response that freqz derives.
        n, fs = 65536, 48000
        imp = np.zeros(n)
        imp[0] = 1.0
        f = np.fft.rfftfreq(n, 1.0 / fs)
        sel = (f >= 5.0) & (f <= 1000.0)
        for secs in (svf.design_butter(4, 60.0, fs, "low"), svf.design_butter(3, 90.0, fs, "high")):
            h = np.fft.rfft(svf.process(secs, imp))
            ref = svf.freqz(secs, f[sel], fs)
            self.assertLess(np.max(np.abs(h[sel] - ref) / np.abs(ref)), 1e-6)

    def test_process_fast_matches_process(self):
        x = np.random.default_rng(0).standard_normal(4096)
        for secs in (svf.design_butter(4, 60.0, 48000, "low"), svf.design_butter(3, 90.0, 48000, "high"),
                     [svf.cytomic("lowshelf", 160.0, 0.71, 9.0, 48000)]):
            self.assertLess(np.max(np.abs(svf.process_fast(secs, x) - svf.process(secs, x))), 1e-9)

    def test_process_varying_equals_static_for_constant_g(self):
        x = np.random.default_rng(1).standard_normal(3000)
        g, k = float(np.tan(np.pi * 80.0 / 48000)), float(np.sqrt(2.0))
        a = svf.process_varying(x, np.full(len(x), g), k, mode="low")
        b = svf.process([svf.lowpass(g, k)], x)
        self.assertLess(np.max(np.abs(a - b)), 1e-12)

    def test_modulated_cutoff_stable(self):
        # Cutoff swept 15..500 Hz at 10 Hz: the TPT form must stay bounded for every output tap.
        fs = 48000
        t = np.arange(fs) / fs
        x = np.random.default_rng(2).standard_normal(fs)
        f_t = 15.0 * (500.0 / 15.0) ** (0.5 * (1.0 + np.sin(2 * np.pi * 10.0 * t)))
        g = np.tan(np.pi * f_t / fs)
        for mode in ("low", "band", "high"):
            y = svf.process_varying(x, g, float(np.sqrt(2.0)), mode=mode)
            self.assertTrue(np.all(np.isfinite(y)), msg=mode)
            self.assertLessEqual(np.max(np.abs(y)), 4.0 * np.max(np.abs(x)), msg=mode)

    def test_group_delay_matches_scipy(self):
        fs = 48000
        f = np.geomspace(10.0, 200.0, 50)
        secs = svf.design_butter(4, 120.0, fs, "low")
        b, a = sps.sos2tf(sps.butter(4, 120.0, btype="low", fs=fs, output="sos"))
        _, gd = sps.group_delay((b, a), w=2 * np.pi * f / fs)
        ref_s = gd / fs
        ours = svf.group_delay_s(secs, f, fs)
        self.assertLess(np.max(np.abs(ours - ref_s) / ref_s), 1e-4)


if __name__ == "__main__":
    unittest.main()
