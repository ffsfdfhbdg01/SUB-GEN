# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the sub-path dynamics (SPEC §7.4 compressor, §7.5 gate, §7.8 limiter).

Static curve (L = detector level dB, T threshold, R ratio, W knee): L for 2(L - T) < -W; L + (1/R - 1)(L - T + W/2)^2/(2W)
inside the knee; T + (L - T)/R above it; GR = output - L. The detectors work over max(10 ms, 1.5 T_sub), T_sub = 1/f_sub1:
a mean square over that window (3 periods of x^2 at f_sub1) followed by a hold over the same window, or a peak hold, so
a steady tone gives a constant level and the gain does not ripple (THD measured: -124 dB at GR 6 dB).
"""
import unittest

import numpy as np

from subgen_proto import dynamics, signals, util

FS = 48000


def _steps(f, levels_db, step_s, fs=FS):
    return np.concatenate([signals.sine(f, fs, step_s, level_db=lv) for lv in levels_db])


class TestStaticCurve(unittest.TestCase):
    def test_formula_and_knee_continuity(self):
        t, r, w = -20.0, 4.0, 6.0
        lv = np.linspace(-40.0, 0.0, 4001)
        gr = dynamics.static_gr_db(lv, t, r, w)
        self.assertTrue(np.all(gr[lv < t - w / 2] == 0.0))
        above = lv > t + w / 2
        np.testing.assert_allclose(gr[above], (t + (lv[above] - t) / r) - lv[above], atol=1e-12)
        self.assertLess(np.max(np.abs(np.diff(gr))), 0.01)  # no step at the knee edges (grid 0.01 dB)
        for edge in (t - w / 2, t + w / 2):  # both branches meet
            self.assertAlmostEqual(float(dynamics.static_gr_db(np.array([edge - 1e-9]), t, r, w)[0]),
                                   float(dynamics.static_gr_db(np.array([edge + 1e-9]), t, r, w)[0]), places=6)
        np.testing.assert_allclose(dynamics.static_gr_db(lv, t, r, 0.0), np.where(lv > t, (1 / r - 1) * (lv - t), 0.0), atol=1e-12)

    def test_comp_static_curve(self):
        # T-CMP-01: a 30 Hz sine in 3 dB steps; the RMS detector reads the peak level - 3.01 dB
        levels = np.arange(-36.0, 1.0, 3.0)
        x = _steps(30.0, levels, 1.0)
        _, gr = dynamics.compressor(x, FS, thresh_db=-20.0, ratio=4.0, knee_db=6.0, attack_ms=10.0, release_ms=100.0, f_sub1=30.0)
        for i, lv in enumerate(levels):
            measured = float(np.median(gr[int((i + 0.7) * FS):int((i + 1.0) * FS)]))
            want = float(dynamics.static_gr_db(np.array([lv + 20.0 * np.log10(np.sqrt(0.5))]), -20.0, 4.0, 6.0)[0])
            self.assertAlmostEqual(measured, want, delta=0.1, msg=f"{lv} dBFS peak")

    def test_peak_detector_reads_the_peak(self):
        x = signals.sine(40.0, FS, 2.0, level_db=-12.0)
        _, gr = dynamics.compressor(x, FS, thresh_db=-20.0, ratio=4.0, knee_db=0.0, attack_ms=10.0, release_ms=100.0, detect="peak")
        self.assertAlmostEqual(float(np.median(gr[FS:])), (1 / 4 - 1) * (-12.0 + 20.0), delta=0.05)


class TestCompressorDistortion(unittest.TestCase):
    def _thd(self, f, f_sub1=None, detect="rms", check_gr=True, **kw):
        # -12 dBFS peak: the RMS detectors read -15.01 dB, the peak detectors -12 dB; the threshold sits 8 dB below
        # that level and R = 4, so the steady GR is 6 dB (checked for the ripple-free detectors)
        x = signals.sine(f, FS, 3.0, level_db=-12.0)
        level = -12.0 if detect.startswith("peak") else -12.0 + 20.0 * np.log10(np.sqrt(0.5))
        y, gr = dynamics.compressor(x, FS, thresh_db=level - 8.0, ratio=4.0, knee_db=0.0, f_sub1=f_sub1 or f, detect=detect, **kw)
        if check_gr:
            self.assertAlmostEqual(float(np.median(gr[FS:2 * FS])), -6.0, delta=0.05)
        return util.thd_db(y[FS:2 * FS], FS, f)

    def test_comp_ripple_rule_thd(self):
        # T-CMP-02 at 30-100 Hz with the fast settings of the brief (attack 1 ms, release 10 ms). With a ripple-free
        # detector the THD stays far below -60 dB with or without the rule (the rule's ~10 dB only shows with
        # ripple-prone detectors, see test_ripple_prone_detectors_need_more_than_the_rule).
        for f in (30.0, 40.0, 60.0, 100.0):
            for rule in (True, False):
                self.assertLessEqual(self._thd(f, attack_ms=1.0, release_ms=10.0, ripple_rule=rule), -60.0, msg=f"{f} Hz rule {rule}")

    def test_off_frequency_tone_does_not_ripple(self):
        # the window is tuned to f_sub1 = 40 Hz; a 48 Hz tone would ripple without the hold stage (-45 dB, measured).
        # The hold reads the top of the mean-square ripple, so the level reads ~0.35 dB high (GR 6.26 instead of 6 dB).
        self.assertLessEqual(self._thd(48.0, f_sub1=40.0, attack_ms=1.0, release_ms=10.0, check_gr=False), -60.0)
        self.assertLessEqual(self._thd(48.0, f_sub1=40.0, attack_ms=1.0, release_ms=10.0, detect="peak"), -60.0)
        x = signals.sine(48.0, FS, 3.0, level_db=-12.0)
        _, gr = dynamics.compressor(x, FS, thresh_db=-12.0 + 20.0 * np.log10(np.sqrt(0.5)) - 8.0, ratio=4.0, knee_db=0.0,
                                    attack_ms=1.0, release_ms=10.0, f_sub1=40.0)
        self.assertAlmostEqual(float(np.median(gr[FS:2 * FS])), -6.0, delta=0.5)

    def test_ripple_prone_detectors_need_more_than_the_rule(self):
        # the SPEC-literal window without hold, a one-pole RMS and an instantaneous peak: kept for the P2 comparison
        for detect in ("rms_window", "rms_onepole", "peak_instant"):
            f = 48.0 if detect == "rms_window" else 40.0
            off = self._thd(f, f_sub1=40.0, attack_ms=1.0, release_ms=10.0, ripple_rule=False, detect=detect, check_gr=False)
            on = self._thd(f, f_sub1=40.0, attack_ms=1.0, release_ms=10.0, ripple_rule=True, detect=detect, check_gr=False)
            self.assertGreater(off, -60.0, msg=detect)
            self.assertLess(on, off - 5.0, msg=detect)  # the rule helps them...
            self.assertGreater(on, -60.0, msg=detect)  # ...but not enough


class TestBallistics(unittest.TestCase):
    def test_smoother_time_constants(self):
        target = np.concatenate([np.zeros(FS // 10), np.full(FS // 2, -10.0), np.zeros(FS // 2)])
        gr = dynamics.smooth_gr(target, FS, attack_ms=5.0, release_ms=50.0)
        n0, n1 = FS // 10, FS // 10 + FS // 2
        self.assertAlmostEqual(gr[n0 + int(0.005 * FS) - 1], -10.0 * (1 - np.exp(-1.0)), delta=0.05)
        self.assertAlmostEqual(gr[n1 + int(0.050 * FS) - 1], -10.0 * np.exp(-1.0), delta=0.05)

    def test_attack_release_time(self):
        # T-CMP-03: a 200 Hz tone (detector window 10 ms, peak reached within T/4 = 1.25 ms) steps up by 20 dB and
        # back; GR reaches 63 % of its change within 10 % of attack (and of release + the 10 ms hold)
        x = _steps(200.0, (-40.0, -10.0, -40.0), 1.0)
        for attack, release in ((20.0, 100.0), (50.0, 300.0)):
            _, gr = dynamics.compressor(x, FS, thresh_db=-30.0, ratio=4.0, knee_db=0.0, attack_ms=attack, release_ms=release,
                                        detect="peak", f_sub1=200.0)
            lo, hi = float(gr[int(0.9 * FS)]), float(gr[int(1.9 * FS)])
            t_att = (np.argmax(gr[FS:] <= lo + 0.632 * (hi - lo))) / FS * 1000.0
            t_rel = (np.argmax(gr[2 * FS:] >= hi + 0.632 * (lo - hi))) / FS * 1000.0
            self.assertLess(abs(t_att / attack - 1.0), 0.10, msg=f"attack {attack}: {t_att:.1f} ms")
            self.assertLess(abs(t_rel / (release + 10.0) - 1.0), 0.10, msg=f"release {release}: {t_rel:.1f} ms")

    def test_ripple_rule_floors_the_time_constants(self):
        target = np.concatenate([np.zeros(FS // 10), np.full(FS, -10.0)])
        att, rel = dynamics.effective_times(1.0, 10.0, 40.0, True)  # floors 0.25 T = 6.25 ms, 1.0 T = 25 ms
        self.assertEqual((att, rel), (6.25, 25.0))
        self.assertEqual(dynamics.effective_times(1.0, 10.0, 40.0, False), (1.0, 10.0))
        self.assertEqual(dynamics.effective_times(20.0, 300.0, 40.0, True), (20.0, 300.0))

    def test_auto_release_depends_on_how_long_the_gain_was_reduced(self):
        # 50 / 500 ms mixed release (SPEC §7.4): a short burst releases fast, a long one slowly
        def release_ms(burst_s):
            x = np.concatenate([signals.sine(100.0, FS, burst_s, level_db=-6.0), signals.sine(100.0, FS, 2.0, level_db=-40.0)])
            _, gr = dynamics.compressor(x, FS, thresh_db=-30.0, ratio=4.0, knee_db=0.0, attack_ms=5.0, release_ms=50.0,
                                        detect="peak", f_sub1=100.0, auto_release=True)
            n0 = int(burst_s * FS)
            lo = float(gr[n0 - 1])
            return np.argmax(gr[n0:] >= lo * np.exp(-1.0)) / FS * 1000.0
        short, long_ = release_ms(0.05), release_ms(3.0)
        self.assertLess(short, 120.0)
        self.assertGreater(long_, 350.0)

    def test_makeup_and_identity(self):
        x = signals.sine(40.0, FS, 1.0, level_db=-40.0)  # far below the threshold: GR = 0
        y, gr = dynamics.compressor(x, FS, thresh_db=-20.0, ratio=4.0, knee_db=6.0, attack_ms=10.0, release_ms=100.0, makeup_db=6.0)
        self.assertTrue(np.all(gr == 0.0))
        np.testing.assert_allclose(y, x * 10 ** (6.0 / 20.0), rtol=1e-12)


class TestGate(unittest.TestCase):
    def test_gate_hysteresis_no_chatter(self):
        # a 100 Hz tone whose level swings +-1 dB around the threshold (2 Hz): with 3 dB hysteresis it opens once
        t = np.arange(3 * FS) / FS
        x = 10 ** ((-50.0 + np.sin(2 * np.pi * 2.0 * t)) / 20.0) * np.sin(2 * np.pi * 100.0 * t)
        _, mask = dynamics.gate(x, FS, thresh_db=-50.0, range_db=-40.0, hold_ms=40.0, release_ms=120.0, f_sub1=100.0)
        self.assertLessEqual(int(np.sum(np.diff(mask.astype(int)) != 0)), 2)

    def test_threshold_hold_and_release(self):
        # |x| = 0.1 (-20 dBFS) for 0.5 s, then silence: the detector window (max(10 ms, 1.5 T) = 15 ms at f_sub1 =
        # 100 Hz) holds the level, then the gate stays open for hold_ms and closes; the gain falls by 63 % of the
        # range within release_ms
        x = np.concatenate([np.full(FS // 2, 0.1), np.zeros(FS)])
        y, mask = dynamics.gate(x, FS, thresh_db=-30.0, range_db=-40.0, hold_ms=40.0, release_ms=120.0, f_sub1=100.0)
        close = int(np.flatnonzero(np.diff(mask.astype(int)) == -1)[0]) + 1
        want = FS // 2 + int(round(max(0.010, 1.5 / 100.0) * FS)) + int(round(0.040 * FS))
        self.assertLessEqual(abs(close - want), 2)
        gain_db = dynamics.gate_gain_db(mask, FS, range_db=-40.0, attack_ms=2.0, release_ms=120.0, f_sub1=100.0)
        k = np.argmax(gain_db[close:] <= -40.0 * (1 - np.exp(-1.0)))
        self.assertLess(abs(k / FS * 1000.0 / 120.0 - 1.0), 0.10)
        self.assertTrue(mask[FS // 4])  # opened above the threshold

    def test_threshold_accuracy(self):
        for level, opens in ((-29.5, True), (-30.5, False)):
            x = signals.sine(100.0, FS, 1.0, level_db=level)
            _, mask = dynamics.gate(x, FS, thresh_db=-30.0, range_db=-40.0, hold_ms=40.0, release_ms=120.0, f_sub1=100.0)
            self.assertEqual(bool(mask[FS // 2:].any()), opens, msg=f"{level} dBFS")

    def test_closed_gate_attenuates_by_the_range(self):
        x = signals.sine(100.0, FS, 2.0, level_db=-60.0)  # below the threshold all along
        y, mask = dynamics.gate(x, FS, thresh_db=-50.0, range_db=-40.0, hold_ms=40.0, release_ms=120.0, f_sub1=100.0)
        self.assertFalse(mask.any())
        self.assertAlmostEqual(20 * np.log10(util.rms(y[FS:]) / util.rms(x[FS:])), -40.0, delta=0.01)


class TestLimiter(unittest.TestCase):
    def test_limiter_ceiling(self):
        # T-LIM-01: a square 6 dB over the ceiling (and a kick, and noise), live (tanh) and lookahead 2 / 5 ms
        c_db = -1.0
        c = 10 ** (c_db / 20.0)
        stress = [signals.square(40.0, FS, 1.0, level_db=c_db + 6.0), signals.kick(FS, n_hits=2, spacing=0.3, level_db=c_db + 9.0),
                  np.random.default_rng(0).standard_normal(FS) * 2.0]
        for x in stress:
            self.assertLessEqual(np.max(np.abs(dynamics.limiter(x, FS, ceiling_db=c_db, lookahead_ms=0.0, release_ms=100.0, live=True))), c + 1e-9)
            for la in (2.0, 5.0):
                y = dynamics.limiter(x, FS, ceiling_db=c_db, lookahead_ms=la, release_ms=100.0)
                self.assertLessEqual(np.max(np.abs(y)), c + 1e-9, msg=f"lookahead {la}")

    def test_limiter_lookahead_delay(self):
        for la in (2.0, 5.0):
            x = signals.impulse(FS // 10, amp=0.5)  # below the ceiling: the limiter only delays
            y = dynamics.limiter(x, FS, ceiling_db=-1.0, lookahead_ms=la, release_ms=100.0)
            d = int(round(la * 1e-3 * FS))
            self.assertEqual(int(np.argmax(np.abs(y))), d)
            self.assertAlmostEqual(float(y[d]), 0.5, places=12)
            self.assertEqual(float(np.max(np.abs(np.delete(y, d)))), 0.0)

    def test_limiter_transparent_below_the_ceiling(self):
        x = signals.sine(40.0, FS, 1.0, level_db=-6.0)
        y = dynamics.limiter(x, FS, ceiling_db=-1.0, lookahead_ms=5.0, release_ms=100.0)
        d = int(round(0.005 * FS))
        np.testing.assert_allclose(y[d:], x[:-d], atol=1e-15)

    def test_limiter_thd_reported_finite(self):
        # T-LIM-02: 40 Hz, ceiling + 6 dB: the THD is measured (the bound comes from P2-09)
        x = signals.sine(40.0, FS, 3.0, level_db=-1.0 + 6.0)
        for kw in (dict(lookahead_ms=0.0, live=True), dict(lookahead_ms=2.0), dict(lookahead_ms=5.0)):
            y = dynamics.limiter(x, FS, ceiling_db=-1.0, release_ms=100.0, **kw)
            self.assertTrue(np.isfinite(util.thd_db(y[FS:2 * FS], FS, 40.0)), msg=str(kw))

    def test_live_limiter_is_tanh_with_adaa(self):
        c = 10 ** (-1.0 / 20.0)
        x = signals.sine(40.0, FS, 0.5, level_db=-30.0)  # small: tanh(x/c) c ~ x
        y = dynamics.limiter(x, FS, ceiling_db=-1.0, lookahead_ms=0.0, release_ms=100.0, live=True)
        mid = 0.5 * (x + np.concatenate([[0.0], x[:-1]]))
        np.testing.assert_allclose(y, c * np.tanh(mid / c), atol=1e-6)  # ADAA ~ tanh at the half-sample midpoint


if __name__ == "__main__":
    unittest.main()
