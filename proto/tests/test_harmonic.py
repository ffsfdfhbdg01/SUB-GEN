# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the HARMONIC engine (SPEC §6.5). For a sine of amplitude A with x_n = x / A, T_k(cos t) = cos(k t), so
the k-th harmonic of  y = A * sum_{k=2..K} w_k T_k(x_n),  w_k = (amount/100) c_k / k,  has amplitude exactly A w_k.
"""
import unittest

import numpy as np

from subgen_proto import harmonic, signals, util

FS = 48000
F0 = 40.0  # 40 whole cycles per second: the analysis window [1, 2] s holds an integer number of cycles


def _amps(y, fs=FS, f0=F0, n=5):
    return util.harmonic_amps(y[int(1.0 * fs):int(2.0 * fs)], fs, f0, n)


def _db(v):
    return 20.0 * np.log10(v)


class TestWeights(unittest.TestCase):
    def test_harmonic_weights_unit_sine(self):
        a_pk = 10.0 ** (-12.0 / 20.0)
        y = harmonic.harmonic(signals.sine(F0, FS, 3.0, level_db=-12.0), FS, amount_pct=100.0, balance=0.0, k_max=5)
        a = _amps(y)
        for k in range(2, 6):  # 1/2, 1/3, 1/4, 1/5 of the envelope
            self.assertAlmostEqual(_db(a[k - 1] / a_pk), -_db(k), delta=0.5, msg=f"harmonic {k}")
        self.assertLess(a[0] / a_pk, 10.0 ** (-50.0 / 20.0))  # the fundamental is not generated

    def test_amount_scales_the_weights_linearly(self):
        x = signals.sine(F0, FS, 3.0, level_db=-12.0)
        full = _amps(harmonic.harmonic(x, FS, amount_pct=100.0, k_max=5))
        half = _amps(harmonic.harmonic(x, FS, amount_pct=50.0, k_max=5))
        for k in range(2, 6):
            self.assertAlmostEqual(_db(full[k - 1] / half[k - 1]), 6.02, delta=0.1, msg=f"harmonic {k}")

    def test_k_max_limits_the_order(self):
        a_pk = 10.0 ** (-12.0 / 20.0)
        a = _amps(harmonic.harmonic(signals.sine(F0, FS, 3.0, level_db=-12.0), FS, amount_pct=100.0, k_max=3))
        self.assertGreater(a[2] / a_pk, 0.3)  # 3rd present (1/3)
        self.assertLess(a[3] / a_pk, 10.0 ** (-80.0 / 20.0))
        self.assertLess(a[4] / a_pk, 10.0 ** (-80.0 / 20.0))

    def test_balance_even_odd(self):
        x = signals.sine(F0, FS, 3.0, level_db=-12.0)
        odd_only = _amps(harmonic.harmonic(x, FS, amount_pct=100.0, balance=100.0, k_max=5))  # c_even = 1 - b = 0
        self.assertLess(max(odd_only[1], odd_only[3]) / min(odd_only[2], odd_only[4]), 1e-3)  # <= -60 dB
        even_only = _amps(harmonic.harmonic(x, FS, amount_pct=100.0, balance=-100.0, k_max=5))  # c_odd = 1 + b = 0
        self.assertLess(max(even_only[2], even_only[4]) / min(even_only[1], even_only[3]), 1e-3)

    def test_level_independent(self):
        a_pk = lambda lv: 10.0 ** (lv / 20.0)
        for level in (-40.0, -24.0, -12.0, -6.0):
            a = _amps(harmonic.harmonic(signals.sine(F0, FS, 3.0, level_db=level), FS, amount_pct=100.0, k_max=5))
            for k in range(2, 6):
                self.assertAlmostEqual(_db(a[k - 1] / a_pk(level)), -_db(k), delta=0.5, msg=f"{level} dBFS, harmonic {k}")

    def test_fs_independent(self):
        for fs in (44100, 96000):
            y = harmonic.harmonic(signals.sine(F0, fs, 3.0, level_db=-12.0), fs, amount_pct=100.0, k_max=5)
            a = _amps(y, fs)
            for k in range(2, 6):
                self.assertAlmostEqual(_db(a[k - 1] / 10.0 ** (-12.0 / 20.0)), -_db(k), delta=0.5, msg=f"fs {fs}, harmonic {k}")


class TestCleaning(unittest.TestCase):
    def test_dc_removed(self):
        # A sine alone has no DC in T_2 (cos 2t); a sine on a DC offset does (about -0.025 before the 15 Hz high-pass).
        for x in (signals.sine(F0, FS, 3.0, level_db=-12.0), signals.sine(F0, FS, 3.0, level_db=-12.0) + 0.03):
            for balance in (0.0, -100.0, 100.0):
                y = harmonic.harmonic(x, FS, amount_pct=100.0, balance=balance, k_max=5)
                self.assertLessEqual(abs(np.mean(y[int(1.0 * FS):int(2.0 * FS)])), 1e-4)

    def test_quiet_floor_and_loud(self):
        # Below E_FLOOR (-80 dBFS) x_n = x / E_FLOOR < 1, so the harmonics collapse (T_k ~ x_n^k): -90 dBFS in.
        quiet = harmonic.harmonic(signals.sine(F0, FS, 3.0, level_db=-90.0), FS, amount_pct=100.0, k_max=5)
        self.assertLessEqual(_db(util.rms(quiet[int(1.5 * FS):])), -110.0)
        loud = harmonic.harmonic(signals.sine(F0, FS, 3.0, level_db=12.0), FS, amount_pct=100.0, k_max=5)  # amplitude 4
        self.assertTrue(np.all(np.isfinite(loud)))
        self.assertLess(np.max(np.abs(loud)), 4.0 * 1.283 * 1.2)  # e * sum(w_k) = 4 * (1/2 + 1/3 + 1/4 + 1/5), 20 % margin

    def test_silence_gives_silence(self):
        np.testing.assert_array_equal(harmonic.harmonic(np.zeros(FS), FS), np.zeros(FS))


class TestAmplitudeEnvelope(unittest.TestCase):
    def test_ripple_free_for_bass_sines(self):
        # The SPEC's plain peak follower (5 / 60 ms) sits 1.05 dB low and ripples 0.2-0.7 dB on these sines (measured):
        # a max-hold over 1.5 periods of the lowest band frequency, then the same smoother, is exact.
        a_pk = 10.0 ** (-12.0 / 20.0)
        for f in (30.0, 40.0, 55.0, 80.0, 120.0):
            e = harmonic.amplitude_envelope(signals.sine(f, FS, 2.0, level_db=-12.0), FS)[int(0.3 * FS):]
            self.assertLess(np.max(np.abs(_db(e / a_pk))), 0.01, msg=f"{f} Hz")

    def test_follows_a_level_drop(self):
        # -6 dBFS for 1 s, then -12 dBFS: held for 50 ms (1.5 periods of 30 Hz), then released with 60 ms.
        x = np.concatenate([signals.sine(F0, FS, 1.0, level_db=-6.0), signals.sine(F0, FS, 2.0, level_db=-12.0)])
        e = harmonic.amplitude_envelope(x, FS)
        self.assertGreater(e[int(1.04 * FS)], 10.0 ** (-6.5 / 20.0))  # still held 40 ms after the drop
        self.assertLess(abs(_db(e[int(1.8 * FS)] / 10.0 ** (-12.0 / 20.0))), 0.1)  # settled 0.8 s after it


if __name__ == "__main__":
    unittest.main()
