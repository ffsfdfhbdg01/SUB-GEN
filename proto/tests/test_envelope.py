# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the envelope followers. Expected values come from the one-pole step response 1 - exp(-t/tau)."""
import unittest

import numpy as np

from subgen_proto import envelope, signals

FS = 48000


class TestPeakFollower(unittest.TestCase):
    def test_attack_step_reaches_63_percent_at_one_time_constant(self):
        # Breaks if the attack coefficient is not 1 - exp(-1/(tau*fs)).
        x = np.ones(FS // 2)
        y = envelope.peak_follower(x, FS, attack_ms=2.0, release_ms=50.0)
        n = int(round(0.002 * FS))
        self.assertLess(abs(y[n - 1] - (1 - np.exp(-1.0))), 0.01)

    def test_release_falls_to_37_percent_at_one_time_constant(self):
        # Breaks if release uses the attack constant (or the reverse).
        x = np.concatenate([np.ones(FS // 2), np.zeros(FS // 2)])
        y = envelope.peak_follower(x, FS, attack_ms=0.1, release_ms=20.0)
        n0 = FS // 2
        n = int(round(0.020 * FS))
        self.assertLess(abs(y[n0 + n - 1] - np.exp(-1.0)), 0.01)

    def test_negative_input_is_rectified(self):
        y = envelope.peak_follower(-np.ones(2000), FS, 1.0, 50.0)
        self.assertGreater(y[-1], 0.99)


class TestSlidingPeak(unittest.TestCase):
    def test_causal_window_maximum(self):
        # y[n] = max(x[n-w+1 .. n]): an impulse at n0 is held for w samples starting at n0 and not before.
        x = np.zeros(100)
        x[10], x[50] = 1.0, 0.5
        y = envelope.sliding_peak(x, 8)
        self.assertTrue(np.all(y[:10] == 0.0))
        self.assertTrue(np.all(y[10:18] == 1.0))
        self.assertTrue(np.all(y[18:50] == 0.0))
        self.assertTrue(np.all(y[50:58] == 0.5))
        self.assertTrue(np.all(y[58:] == 0.0))

    def test_window_of_one_is_the_identity(self):
        x = np.random.default_rng(0).standard_normal(50)
        np.testing.assert_array_equal(envelope.sliding_peak(x, 1), x)

    def test_causal_at_the_start_of_the_buffer(self):
        # D8 M1: an impulse inside the first w - 1 samples must not show up before it happens (no reflection).
        x = np.zeros(20)
        x[5] = 1.0
        y = envelope.sliding_peak(x, 8)
        self.assertTrue(np.all(y[:5] == 0.0))
        self.assertTrue(np.all(y[5:13] == 1.0))
        self.assertTrue(np.all(y[13:] == 0.0))

    def test_initial_value_counts_before_the_start(self):
        # samples before the buffer count as `initial` (the limiter's held gain starts at 1)
        y = envelope.sliding_peak(np.zeros(10), 4, initial=1.0)
        self.assertTrue(np.all(y[:3] == 1.0))
        self.assertTrue(np.all(y[3:] == 0.0))


class TestRmsFollower(unittest.TestCase):
    def test_rms_of_sine_is_amplitude_over_sqrt2(self):
        x = signals.sine(200.0, FS, 1.0, level_db=0.0)
        y = envelope.rms_follower(x, FS, tau_ms=20.0)
        self.assertLess(abs(y[-1] - 2 ** -0.5), 0.02)


if __name__ == "__main__":
    unittest.main()
