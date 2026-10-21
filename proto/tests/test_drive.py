# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for Drive / Saturate (SPEC §7.3). Reference level: x_ref = x * 10^(12/20), so a -12 dBFS-peak sine is +-1.0
in the shaper's domain; pre-gain g = 10^((30 drive/100)/20); blend alpha = min(1, drive/5); output / 10^(12/20)."""
import unittest

import numpy as np
from scipy import signal as sps

from subgen_proto import drive, signals, util

FS = 48000
MODES = ("soft", "tube", "tape", "fold", "hard")


def _ref(fs=FS, f=55.0, dur=2.0):
    return signals.sine(f, fs, dur, level_db=-12.0)  # amplitude 0.2512 = 1.0 at the reference level


def _rms_db(y, fs=FS):
    return 20.0 * np.log10(util.rms(y[fs:]))  # the last second: past every filter transient


class TestShapes(unittest.TestCase):
    def test_antiderivatives_numeric(self):
        # (F1(x + h) - F1(x - h)) / 2h = f(x) for every mode and bias; the grid keeps 3.7e-4 away from the kinks of Hard
        # (at +-1 - b), where a central difference of a piecewise function is only O(h) accurate.
        h = 1e-4
        x = np.arange(-600, 601) * 0.01 + 0.00037
        for mode in MODES:
            for bias in (0.0, 0.35, 0.5):
                f, f1 = drive.shape_fn(mode, bias)
                err = np.max(np.abs((f1(x + h) - f1(x - h)) / (2.0 * h) - f(x)))
                self.assertLess(err, 1e-6, msg=f"{mode} bias {bias}")

    def test_shape_values(self):
        pi = np.pi
        for mode in MODES:
            for bias in (0.0, 0.35):
                self.assertAlmostEqual(float(drive.shape_fn(mode, bias)[0](0.0)), 0.0, places=15, msg=f"{mode} {bias}")
        f = lambda mode, b=0.0: drive.shape_fn(mode, b)[0]
        self.assertAlmostEqual(float(f("soft")(1.0)), np.tanh(1.0), places=14)
        self.assertAlmostEqual(float(f("tape")(1.0)), 2.0 / pi * np.arctan(pi / 2.0), places=14)
        self.assertAlmostEqual(float(f("fold")(1.0)), 1.0, places=14)  # sin(pi/2)
        self.assertAlmostEqual(float(f("fold")(2.0)), 0.0, places=14)  # sin(pi)
        self.assertEqual([float(f("hard")(v)) for v in (-2.0, -0.3, 0.3, 2.0)], [-1.0, -0.3, 0.3, 1.0])
        self.assertAlmostEqual(float(f("soft", 0.35)(0.2)), np.tanh(0.55) - np.tanh(0.35), places=14)  # f(x + b) - f(b)

    def test_effective_bias_formula(self):
        # b_eff = b_mode (1 - max(0, bal)/100) + 0.5 max(0, -bal)/100, b_mode = 0.35 for Tube, else 0.
        cases = [("tube", 0.0, 0.35), ("tube", 100.0, 0.0), ("tube", -100.0, 0.35 + 0.5), ("tube", 50.0, 0.175),
                 ("tube", -50.0, 0.35 + 0.25), ("soft", 0.0, 0.0), ("soft", 100.0, 0.0), ("soft", -50.0, 0.25)]
        for mode, bal, want in cases:
            self.assertAlmostEqual(drive.effective_bias(mode, bal), want, places=12, msg=f"{mode} {bal}")

    def test_log_cosh_is_stable_where_cosh_overflows(self):
        f1 = drive.shape_fn("soft", 0.0)[1]
        self.assertAlmostEqual(float(f1(800.0)), 800.0 - np.log(2.0), places=9)
        self.assertAlmostEqual(float(f1(-800.0)), 800.0 - np.log(2.0), places=9)
        self.assertAlmostEqual(float(f1(0.5)), np.log(np.cosh(0.5)), places=14)


class TestProcess(unittest.TestCase):
    def test_drive_zero_identity(self):
        x = np.random.default_rng(0).standard_normal(4800) * 0.2
        for mode in MODES:
            for os_factor in (1, 2, 4):
                for balance in (-100.0, 0.0, 100.0):
                    y = drive.process(x, FS, drive_pct=0.0, mode=mode, balance=balance, os_factor=os_factor)
                    self.assertTrue(np.array_equal(y, x), msg=f"{mode} os {os_factor} balance {balance}")

    def test_alpha_fade_continuity(self):
        # alpha = min(1, drive/5) blends the shaper in; its slope (0.2 per 1 % drive) must not make the output jump.
        x = _ref()
        y = {d: drive.process(x, FS, drive_pct=d, mode="soft") for d in (4.9, 4.99, 5.0)}
        rel = lambda a, b: util.rms((y[a] - y[b])[FS:]) / util.rms(y[b][FS:])
        self.assertLess(rel(4.99, 5.0), 1e-3)
        self.assertLess(rel(4.9, 5.0), 1e-2)
        self.assertLess(rel(4.99, 5.0), 0.2 * rel(4.9, 5.0))  # the change scales with the step: no jump

    def test_auto_gain_rms(self):
        # T-DRV-03: after the DC blocker the reference sine keeps its RMS within 0.5 dB, drive 10..100 %, every mode
        # and bias. The creative Tone filter is not compensated (tone_hz=None).
        x = _ref()
        ref = _rms_db(x)
        for mode in MODES:
            for d in (10.0, 25.0, 50.0, 75.0, 100.0):
                for balance in (-100.0, 0.0, 100.0):
                    y = drive.dc_block(drive.process(x, FS, drive_pct=d, mode=mode, balance=balance, tone_hz=None), FS)
                    self.assertAlmostEqual(_rms_db(y), ref, delta=0.5, msg=f"{mode} drive {d} balance {balance}")

    def test_auto_gain_off_leaves_the_raw_level(self):
        x = _ref()
        on = drive.process(x, FS, drive_pct=100.0, mode="soft", tone_hz=None)
        off = drive.process(x, FS, drive_pct=100.0, mode="soft", tone_hz=None, auto_gain=False)
        self.assertAlmostEqual(_rms_db(on) - _rms_db(off), drive.auto_gain_db("soft", 100.0, 0.0), delta=1e-9)
        self.assertLess(drive.auto_gain_db("soft", 100.0, 0.0), -2.0)  # tanh(31.6 sin) is a near-square wave: +3 dB

    def test_dc_after_tube(self):
        x = _ref()
        for d in (50.0, 100.0):
            y = drive.process(x, FS, drive_pct=d, mode="tube", tone_hz=None)
            self.assertGreater(abs(np.mean(y[FS:])), 1e-3)  # the bias makes DC...
            self.assertLessEqual(abs(np.mean(drive.dc_block(y, FS)[FS:])), 1e-4)  # ...the 5 Hz high-pass removes it

    def test_adaa_reduces_aliasing_hard(self):
        # 81 Hz: fs / f0 = 592.6 is not an integer, so aliased lines fall between the harmonic lines and can be told
        # apart (80 Hz at 48 kHz is blind: every alias lands on a harmonic). Measured: naive -56.5, ADAA -64.3 dB.
        x = signals.sine(81.0, FS, 1.5, level_db=-12.0)
        kw = dict(drive_pct=100.0, mode="hard", tone_hz=None)
        naive = drive.nonharmonic_db(x, FS, 81.0, adaa=False, **kw)
        adaa = drive.nonharmonic_db(x, FS, 81.0, adaa=True, **kw)
        self.assertLessEqual(adaa, naive - 6.0)

    def test_oversampling_and_adaa_compound(self):
        # Each doubling of the rate removes >= 10 dB of aliasing from the naive shaper, ADAA a further >= 10 dB at 2x.
        x = signals.sine(81.0, FS, 1.5, level_db=-12.0)
        a = lambda **p: drive.nonharmonic_db(x, FS, 81.0, drive_pct=100.0, mode="hard", tone_hz=None, **p)
        naive = [a(os_factor=o, adaa=False) for o in (1, 2, 4)]
        self.assertLessEqual(naive[1], naive[0] - 10.0)
        self.assertLessEqual(naive[2], naive[1] - 10.0)
        self.assertLessEqual(a(os_factor=2, adaa=True), naive[1] - 10.0)

    def test_alias_metric_reaches_its_floor_on_the_reference_itself(self):
        x = signals.sine(80.0, FS, 1.5, level_db=-12.0)
        self.assertLess(drive.alias_error_db(x, FS, drive_pct=100.0, mode="hard", tone_hz=None, os_factor=16, adaa=True), -120.0)

    def test_fs_44100_96000(self):
        for fs in (44100, 96000):
            x = signals.sine(55.0, fs, 2.0, level_db=-12.0)
            self.assertTrue(np.array_equal(drive.process(x, fs, drive_pct=0.0, mode="tape"), x))
            for mode in ("soft", "fold", "hard"):
                y = drive.dc_block(drive.process(x, fs, drive_pct=50.0, mode=mode, tone_hz=None), fs)
                self.assertAlmostEqual(_rms_db(y, fs), _rms_db(x, fs), delta=0.5, msg=f"fs {fs} {mode}")

    def test_tone_filter_is_a_second_order_low_pass_that_fades_with_drive(self):
        x = signals.sine(1000.0, FS, 2.0, level_db=-12.0)
        kw = dict(mode="soft", auto_gain=False)
        flat = drive.process(x, FS, drive_pct=100.0, tone_hz=None, **kw)
        dull = drive.process(x, FS, drive_pct=100.0, tone_hz=100.0, **kw)
        # tanh(31.6 sin) is a near-square wave: its 1 kHz fundamental goes through the 100 Hz LP at -40 dB (12 dB/oct)
        self.assertLess(_rms_db(dull) - _rms_db(flat), -35.0)
        faint_flat = drive.process(x, FS, drive_pct=0.05, tone_hz=None, **kw)  # alpha = 0.01: the filter is 1 % wet
        faint_dull = drive.process(x, FS, drive_pct=0.05, tone_hz=100.0, **kw)
        self.assertLess(util.rms((faint_dull - faint_flat)[FS // 2:]) / util.rms(faint_flat[FS // 2:]), 0.02)

    def test_oversampling_filters_are_flat_in_band_and_deep_in_the_stopband(self):
        # Passband to 0.45 fs, stopband from 0.55 fs (the transition is centred on fs/2 of the base rate).
        for os_factor in (2, 4, 16):
            taps = drive.oversampling_taps(os_factor)
            w, h = sps.freqz(taps, worN=8192, fs=float(os_factor))  # frequency in units of the base rate
            mag = 20.0 * np.log10(np.abs(h) + 1e-300)
            self.assertLess(np.max(np.abs(mag[w <= 0.45])), 0.01, msg=f"os {os_factor}")
            self.assertLess(np.max(mag[w >= 0.55]), -100.0, msg=f"os {os_factor}")



def _tone(f, fs, level_db, dur=1.5):
    return 10.0 ** (level_db / 20.0) * np.sin(2.0 * np.pi * f * np.arange(int(dur * fs)) / fs)


class TestHotInputs(unittest.TestCase):
    """R3 (D8 I2): sub_level (up to +18 dB) sits before the Drive, so inputs above the -6 dBFS reference are real. The
    criterion (D2) holds below 500 Hz for inputs up to +12 dBFS: <= -80 dB (Live), <= -90 dB (Mix, Print)."""

    def test_fold_aliasing_stays_out_of_the_sub_band_for_loud_inputs(self):
        for fs in (44100, 48000):
            for f in (31.0, 81.0, 121.0):
                for level in (6.0, 12.0):
                    for os_factor, bound in ((1, -80.0), (2, -90.0), (4, -90.0)):
                        v = drive.nonharmonic_db(_tone(f, fs, level), fs, f, f_hi=500.0, drive_pct=100.0, mode="fold",
                                                 tone_hz=None, os_factor=os_factor, adaa=True)
                        self.assertLessEqual(v, bound, msg=f"fs {fs} f {f} level {level} os {os_factor}")

    def test_fold_up_to_the_reference_level_is_unchanged(self):
        x = _tone(81.0, 48000, -6.0)  # peak 0.5: x_ref peak 2.0 = the cap
        np.testing.assert_array_equal(drive.process(x, 48000, drive_pct=100.0, mode="fold"),
                                      drive.process(x, 48000, drive_pct=100.0, mode="fold", fold_cap=False))

    def test_other_modes_meet_the_criterion_at_plus_12_dbfs(self):
        for mode in ("soft", "tube", "tape", "hard"):
            v = drive.nonharmonic_db(_tone(121.0, 44100, 12.0), 44100, 121.0, f_hi=500.0, drive_pct=100.0, mode=mode,
                                     tone_hz=None, os_factor=1, adaa=True)
            self.assertLessEqual(v, -80.0, msg=mode)


if __name__ == "__main__":
    unittest.main()
