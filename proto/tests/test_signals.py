# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the SIG-xx test-signal generators."""
import unittest

import numpy as np

from subgen_proto import signals, util

FS = 48000


def _pk(db):
    return 10 ** (db / 20.0)


class TestBasics(unittest.TestCase):
    def test_sine_peak_and_phase(self):
        x = signals.sine(100.0, FS, 0.1, level_db=-6.0, phase=np.pi / 2)
        self.assertLess(abs(np.max(np.abs(x)) - _pk(-6.0)), 1e-9)
        self.assertLess(abs(x[0] - _pk(-6.0)), 1e-12)

    def test_impulse_silence_dc(self):
        imp = signals.impulse(100, 0.5)
        self.assertEqual(np.count_nonzero(imp), 1)
        self.assertEqual(imp[0], 0.5)
        self.assertEqual(np.count_nonzero(signals.silence(FS, 0.1)), 0)
        d = signals.dc(FS, 0.1, 0.5)
        self.assertTrue(np.all(d == 0.5))

    def test_square_is_hard_clipped(self):
        # Flat tops: only the values +/-amp occur (needed for the clipped-bass review item).
        x = signals.square(40.0, FS, 0.1, level_db=0.0)
        self.assertEqual(set(np.round(np.unique(np.abs(x)), 12)), {1.0})

    def test_antiphase_pair_exact(self):
        l, r = signals.antiphase_pair(FS, 60.0, 1.0)
        self.assertTrue(np.array_equal(r, -l))
        self.assertLess(abs(np.max(np.abs(l)) - _pk(-12.0)), 1e-9)

    def test_log_sweep_endpoints(self):
        x = signals.log_sweep(10.0, 500.0, FS, 20.0)
        n = len(x)
        self.assertLess(abs(util.zc_freq(x[: int(0.05 * n)], FS) - 10.5) / 10.5, 0.1)
        f_end = util.zc_freq(x[-int(0.02 * n):], FS)
        self.assertLess(abs(f_end - 495.0) / 495.0, 0.05)


class TestKick(unittest.TestCase):
    def test_kick_deterministic_peak_and_length(self):
        a = signals.kick(FS, n_hits=3, spacing=0.5, level_db=-6.0)
        b = signals.kick(FS, n_hits=3, spacing=0.5, level_db=-6.0)
        self.assertTrue(np.array_equal(a, b))
        self.assertEqual(len(a), 3 * 24000)
        self.assertLessEqual(np.max(np.abs(a)), _pk(-6.0) + 1e-12)

    def test_kick_per_hit_levels(self):
        x = signals.kick(FS, n_hits=3, spacing=0.5, levels_db=[-24.0, -12.0, -3.0])
        for i, db in enumerate([-24.0, -12.0, -3.0]):
            hit = x[i * 24000:(i + 1) * 24000]
            self.assertLess(abs(20 * np.log10(np.max(np.abs(hit))) - db), 0.05)

    def test_kick_pitch_drop_reaches_end_frequency(self):
        # f(t) = 45 + 105*exp(-t/0.030): 45.13 Hz at 0.2 s. Rising zero crossings fall at 7.2 ms and 15.8 ms
        # (hand-solved phase equation), i.e. 116.6 Hz average over the first 20 ms.
        x = signals.kick(FS, n_hits=1, spacing=0.5, level_db=-6.0)
        late = util.zc_freq(x[int(0.15 * FS):int(0.30 * FS)], FS)
        early = util.zc_freq(x[: int(0.020 * FS)], FS)
        self.assertLess(abs(late - 45.0), 1.0)
        self.assertGreater(early, 100.0)


class TestBass(unittest.TestCase):
    def test_bass_808_fundamental_and_second_harmonic(self):
        x = signals.bass_808(FS, f_end=45.0, glide_from=90.0, glide_ms=80.0, harm2_db=-8.0, sustain=0.6, level_db=-6.0)
        seg = x[int(0.25 * FS):int(0.58 * FS)]  # sustain lasts 0.6 s; stay clear of the release
        f, a1 = util.peak_freq(seg, FS, 40.0, 50.0)
        self.assertLess(abs(f - 45.0), 0.2)
        amps = util.harmonic_amps(seg, FS, f, n=2)
        self.assertLess(abs(20 * np.log10(amps[1] / amps[0]) - (-8.0)), 0.5)

    def test_bass_pluck_fundamental_and_determinism(self):
        a = signals.bass_pluck(FS, 55.0, 0.8, seed=0)
        b = signals.bass_pluck(FS, 55.0, 0.8, seed=0)
        self.assertTrue(np.array_equal(a, b))
        f, _ = util.peak_freq(a[int(0.1 * FS):int(0.6 * FS)], FS, 40.0, 70.0)
        self.assertLess(abs(f - 55.0), 0.5)

    def test_bass_pluck_xorshift_burst_is_reproducible(self):
        # the C++ tests build SIG-05 with the xorshift burst (tests/support/Signals: bassPluck)
        a = signals.bass_pluck(FS, 41.2, noise="xorshift", seed=1)
        self.assertTrue(np.array_equal(a, signals.bass_pluck(FS, 41.2, noise="xorshift", seed=1)))
        self.assertFalse(np.array_equal(a, signals.bass_pluck(FS, 41.2, noise="xorshift", seed=2)))
        self.assertAlmostEqual(float(np.max(np.abs(a))), 10 ** (-6 / 20), places=12)
        with self.assertRaises(ValueError):
            signals.bass_pluck(FS, 41.2, noise="xorshift", seed=0)

    def test_chord_three_equal_tones(self):
        x = signals.chord(FS, (55, 69, 82), 2.0, level_db=-12.0)
        amps = [util.peak_freq(x, FS, f - 3.0, f + 3.0)[1] for f in (55.0, 69.0, 82.0)]
        spread_db = 20 * np.log10(max(amps) / min(amps))
        self.assertLess(spread_db, 0.3)

    def test_two_tone_levels_sum_peak_bounded(self):
        x = signals.two_tone(50.0, 70.0, FS, 2.0, level_db=-12.0)
        self.assertLessEqual(np.max(np.abs(x)), _pk(-12.0) + 1e-9)
        a1 = util.peak_freq(x, FS, 45.0, 55.0)[1]
        a2 = util.peak_freq(x, FS, 65.0, 75.0)[1]
        self.assertLess(abs(a1 - _pk(-12.0) / 2) / (_pk(-12.0) / 2), 0.01)
        self.assertLess(abs(a2 - _pk(-12.0) / 2) / (_pk(-12.0) / 2), 0.01)

    def test_kick_plus_808_peak_and_determinism(self):
        a = signals.kick_plus_808(FS)
        b = signals.kick_plus_808(FS)
        self.assertTrue(np.array_equal(a, b))
        self.assertLess(abs(np.max(np.abs(a)) - _pk(-3.0)), 1e-9)


class TestNoise(unittest.TestCase):
    def test_pink_noise_band_limited_and_peak(self):
        x = signals.pink_noise(FS, 8.0, 20.0, 2000.0, level_db=-24.0, seed=1)
        self.assertLess(abs(np.max(np.abs(x)) - _pk(-24.0)), 1e-9)
        self.assertTrue(np.array_equal(x, signals.pink_noise(FS, 8.0, 20.0, 2000.0, level_db=-24.0, seed=1)))
        self.assertFalse(np.array_equal(x, signals.pink_noise(FS, 8.0, 20.0, 2000.0, level_db=-24.0, seed=2)))
        inband = util.band_db(x, FS, 20.0, 2000.0)
        outband = util.band_db(x, FS, 4000.0, 20000.0)
        self.assertLess(outband - inband, -40.0)

    def test_pink_noise_equal_energy_per_octave(self):
        # Breaks if the spectrum is white (energy per octave would rise 3 dB/octave).
        x = signals.pink_noise(FS, 16.0, 20.0, 2000.0, level_db=-24.0, seed=3)
        e1 = util.band_db(x, FS, 40.0, 80.0)
        e2 = util.band_db(x, FS, 160.0, 320.0)
        e3 = util.band_db(x, FS, 640.0, 1280.0)
        self.assertLess(max(e1, e2, e3) - min(e1, e2, e3), 1.5)

    def test_sine_in_noise_snr(self):
        x = signals.sine_in_noise(60.0, FS, 4.0, snr_db=10.0, level_db=-12.0, seed=2)
        s = signals.sine(60.0, FS, 4.0, level_db=-12.0)
        noise = x - s
        snr = 20 * np.log10(util.rms(s) / util.rms(noise))
        self.assertLess(abs(snr - 10.0), 0.2)


class TestPathological(unittest.TestCase):
    def test_denormal_ramp_reaches_subnormal(self):
        r = signals.denormal_ramp(2000)
        nz = np.abs(r[r != 0])
        self.assertTrue(np.all(np.isfinite(r)))
        self.assertLess(nz.min(), 2.2250738585072014e-308)
        self.assertGreater(nz.min(), 0.0)

    def test_nan_inf_contains_all_patterns(self):
        x = signals.nan_inf(1000)
        self.assertTrue(np.any(np.isnan(x)))
        self.assertTrue(np.any(x == np.inf))
        self.assertTrue(np.any(x == -np.inf))
        self.assertGreater(np.count_nonzero(np.isfinite(x)), 900)



class TestXorshift(unittest.TestCase):
    """xorshift64* noise and the band noise built on it: the C++ test generators reproduce both bit for bit."""

    def test_xorshift_noise_known_values(self):
        # seed 1, computed by hand from x ^= x >> 12; x ^= x << 25; x ^= x >> 27; y = (x * 0x2545F4914F6CDD1D) >> 11
        self.assertEqual(signals.xorshift_noise(4, seed=1).tolist(),
                         [-0.43832989989928106, 0.34227450605335275, 0.45169229056673355, -0.39294140006840195])

    def test_xorshift_noise_range_mean_and_seed(self):
        x = signals.xorshift_noise(100000, seed=3)
        self.assertTrue(np.all((x >= -1.0) & (x < 1.0)))
        self.assertLess(abs(np.mean(x)), 0.01)
        self.assertFalse(np.array_equal(x[:100], signals.xorshift_noise(100, seed=4)))
        with self.assertRaises(ValueError):
            signals.xorshift_noise(10, seed=0)

    def test_band_noise_band_limited_and_peak(self):
        x = signals.band_noise(FS, 1.0, 30.0, 120.0, level_db=-12.0, seed=4)
        self.assertEqual(len(x), FS)
        self.assertLess(abs(np.max(np.abs(x)) - _pk(-12.0)), 1e-12)
        inside = util.band_db(x, FS, 30.0, 120.0)
        self.assertLess(util.band_db(x, FS, 240.0, FS / 2), inside - 100.0)  # exactly band-limited (n-point DFT)
        self.assertLess(util.band_db(x, FS, 0.0, 15.0), inside - 100.0)
        np.testing.assert_array_equal(x, signals.band_noise(FS, 1.0, 30.0, 120.0, level_db=-12.0, seed=4))


if __name__ == "__main__":
    unittest.main()
