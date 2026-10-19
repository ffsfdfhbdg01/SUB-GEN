# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the source band-pass x_b (SPEC §5.3). Butterworth magnitudes are derived by hand:
|H_hp(f)|^2 = 1/(1 + (fc/f)^(2n)), |H_lp(f)|^2 = 1/(1 + (f/fc)^(2n)); every corner is -3.01 dB."""
import unittest

import numpy as np

from subgen_proto import band, signals, util

FS = 48000


def _gain_db(f, **kw):
    x = signals.sine(f, FS, 3.0, level_db=-12.0)
    y = band.range_filter(x, FS, **kw)
    seg = slice(int(1.5 * FS), None)  # steady state: 1.5 s >> the 30 Hz time constants
    return 20.0 * np.log10(util.rms(y[seg]) / util.rms(x[seg]))


class TestRangeFilter(unittest.TestCase):
    def test_corner_gains_are_minus_3_db(self):
        # At 30 Hz the 120 Hz low-pass costs 10*log10(1 + (30/120)^8) = 0.0001 dB, and vice versa at 120 Hz.
        for f in (30.0, 120.0):
            self.assertAlmostEqual(_gain_db(f), -3.01, delta=0.05, msg=f"corner {f} Hz")

    def test_passband_is_flat(self):
        # At 60 Hz each side costs 10*log10(1 + 0.5^8) = 0.017 dB, so 0.034 dB in total.
        self.assertAlmostEqual(_gain_db(60.0), -0.034, delta=0.03)

    def test_skirts_fall_24_db_per_octave(self):
        # One octave outside either corner of a 4th-order section: 10*log10(1 + 2^8) = 24.1 dB.
        self.assertAlmostEqual(_gain_db(15.0), -24.1, delta=0.3)
        self.assertAlmostEqual(_gain_db(240.0), -24.1, delta=0.3)

    def test_corner_frequencies_are_parameters(self):
        self.assertAlmostEqual(_gain_db(50.0, lo=50.0, hi=200.0), -3.01, delta=0.05)
        self.assertAlmostEqual(_gain_db(200.0, lo=50.0, hi=200.0), -3.01, delta=0.05)


if __name__ == "__main__":
    unittest.main()
