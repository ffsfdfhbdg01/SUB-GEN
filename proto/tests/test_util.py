# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for measurement helpers. Expected values are derived by hand."""
import unittest

import numpy as np
from scipy import signal as sps

from subgen_proto import util


def _sine(f, fs, dur, amp=1.0, phase=0.0):
    t = np.arange(int(round(fs * dur))) / fs
    return amp * np.sin(2 * np.pi * f * t + phase)


class TestPeakFreq(unittest.TestCase):
    def test_peak_freq_accuracy(self):
        # Breaks if the peak is not interpolated (bin = 0.5 Hz at 2 s) or the window gain is wrong.
        x = _sine(40.3, 48000, 2.0, amp=0.501187)
        f, a = util.peak_freq(x, 48000, 20.0, 100.0)
        self.assertLess(abs(f - 40.3), 0.01)
        self.assertLess(abs(a - 0.501187) / 0.501187, 0.01)

    def test_peak_freq_respects_search_band(self):
        # Breaks if the search ignores f_lo/f_hi and returns the global maximum (40 Hz).
        x = _sine(40.0, 48000, 2.0, amp=1.0) + _sine(90.0, 48000, 2.0, amp=0.5)
        f, a = util.peak_freq(x, 48000, 60.0, 120.0)
        self.assertLess(abs(f - 90.0), 0.02)
        self.assertLess(abs(a - 0.5) / 0.5, 0.01)


class TestZeroCrossings(unittest.TestCase):
    def test_zero_crossings_fractional_position(self):
        # sin(2*pi*100*t + 0.3): first rising crossing at t = (1 - 0.3/(2*pi))/100 s = 457.0818 samples @48k.
        x = _sine(100.0, 48000, 0.1, phase=0.3)
        zc = util.zero_crossings(x, rising=True)
        self.assertLess(abs(zc[0] - 457.0818), 1e-3)
        self.assertLess(abs((zc[1] - zc[0]) - 480.0), 1e-3)

    def test_falling_crossings_are_half_period_after_rising(self):
        # 100 Hz @ 48 kHz: period 480 samples, so a falling crossing sits 240 samples after every rising one.
        x = _sine(100.0, 48000, 0.1)
        rise = util.zero_crossings(x, rising=True)
        fall = util.zero_crossings(x, rising=False)
        for r in rise[:3]:
            self.assertLess(np.min(np.abs(fall - (r + 240.0))), 1e-3)

    def test_zc_freq_sine(self):
        x = _sine(31.7, 44100, 1.0)
        self.assertLess(abs(util.zc_freq(x, 44100) - 31.7), 1e-3)


class TestNull(unittest.TestCase):
    def test_null_db_identical_hits_floor(self):
        x = np.random.default_rng(0).standard_normal(1000)
        self.assertLessEqual(util.null_db(x, x.copy()), -300.0)

    def test_null_db_known_error(self):
        # test = ref * 1.001  ->  error rms / ref rms = 1e-3  ->  -60 dB.
        x = np.random.default_rng(1).standard_normal(5000)
        self.assertLess(abs(util.null_db(x, x * 1.001) - (-60.0)), 0.01)


class TestHarmonics(unittest.TestCase):
    def test_harmonic_amps_and_thd(self):
        # fundamental 1.0, 3rd harmonic 0.01 -> THD = 20*log10(0.01) = -40 dB.
        x = _sine(60.0, 48000, 2.0, 1.0) + _sine(180.0, 48000, 2.0, 0.01)
        amps = util.harmonic_amps(x, 48000, 60.0, n=5)
        self.assertEqual(len(amps), 5)
        self.assertLess(abs(amps[0] - 1.0), 0.01)
        self.assertLess(amps[1], 1e-4)
        self.assertLess(abs(amps[2] - 0.01) / 0.01, 0.03)
        self.assertLess(abs(util.thd_db(x, 48000, 60.0, n=5) - (-40.0)), 0.2)


class TestBandDb(unittest.TestCase):
    def test_band_db_in_and_out_of_band(self):
        # amplitude 0.5 sine: rms = 0.5/sqrt(2) -> -9.03 dBFS. Integer cycles, so no leakage.
        x = _sine(100.0, 48000, 1.0, 0.5)
        self.assertLess(abs(util.band_db(x, 48000, 50.0, 200.0) - (-9.03)), 0.1)
        y = _sine(300.0, 48000, 1.0, 0.5)
        self.assertLess(util.band_db(y, 48000, 50.0, 200.0), -150.0)


class TestFracDelay(unittest.TestCase):
    def test_frac_delay_roundtrip(self):
        # y[n] = x(n - 0.5) for a periodic sine with 5 cycles in 1024 samples.
        n = np.arange(1024)
        x = np.sin(2 * np.pi * 5 * n / 1024)
        expected = np.sin(2 * np.pi * 5 * (n - 0.5) / 1024)
        self.assertLess(np.max(np.abs(util.frac_delay(x, 0.5) - expected)), 1e-9)


class TestAWeighting(unittest.TestCase):
    def _gain_db(self, f, fs=48000):
        sos = util.a_weight_sos(fs)
        _, h = sps.sosfreqz(sos, worN=[f], fs=fs)
        return 20 * np.log10(abs(h[0]))

    def test_a_weight_gain(self):
        # IEC 61672: 1 kHz = 0 dB, 100 Hz = -19.1 dB, 50 Hz = -30.2 dB.
        self.assertLess(abs(self._gain_db(1000.0)), 0.05)
        self.assertLess(abs(self._gain_db(100.0) - (-19.1)), 0.3)
        self.assertLess(abs(self._gain_db(50.0) - (-30.2)), 0.3)

    def test_loudness_a_db_of_sines(self):
        # amplitude-1 sine: rms -3.01 dB; at 50 Hz the A filter removes 30.2 dB more.
        self.assertLess(abs(util.loudness_a_db(_sine(1000.0, 48000, 1.0), 48000) - (-3.01)), 0.1)
        self.assertLess(abs(util.loudness_a_db(_sine(50.0, 48000, 2.0), 48000) - (-33.2)), 0.5)


if __name__ == "__main__":
    unittest.main()
