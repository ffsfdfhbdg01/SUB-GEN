# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the pitch trackers and the TRACK oscillator (SPEC §6.3).

The detectors consume x_b (the range-filtered source), as in the plug-in. Frame grid: hop 2 ms; a frame stamped t
holds the last W decimated samples up to t (zero before the start). Expected values are derived in the comments.
"""
import unittest

import numpy as np
from scipy import signal as sps

from subgen_proto import band, signals, track, util

FS = 48000
CENTS_0_5_PCT = 8.7  # 1200*log2(1.005) = 8.63 cents, the 0.5 % accuracy target of T-TRK-01


def _const(f):
    return lambda t: np.full(np.shape(t), float(f))


def _truth_glide(t):
    """80 Hz until 0.1 s, then log-linear down to 40 Hz at 0.3 s, then 40 Hz."""
    return 80.0 * 2.0 ** (-np.clip((np.asarray(t, dtype=float) - 0.1) / 0.2, 0.0, 1.0))


def _glide_signal(dur=0.6):
    t = np.arange(int(dur * FS)) / FS
    return 0.25 * np.sin(2.0 * np.pi * np.cumsum(_truth_glide(t)) / FS)


def _truth_808(f_end, glide_from, glide_ms=80.0, dur=0.8):
    tau = glide_ms / 1000.0 / 3.0

    def truth(t):
        t = np.asarray(t, dtype=float)
        f = f_end + (glide_from - f_end) * np.exp(-np.maximum(t, 0.0) / tau)
        return np.where((t >= 0.0) & (t < dur), f, np.nan)

    return truth


def _midi_hz(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


class TestEvaluate(unittest.TestCase):
    T = np.arange(0.0, 0.6, 0.002)

    def _track(self, f):
        return track.Track(self.T, np.asarray(f, dtype=float), np.ones_like(self.T))

    def test_perfect_track(self):
        ev = track.evaluate(self._track(_truth_glide(self.T)), _truth_glide, onset_t=0.0)
        self.assertLess(ev["cents_median"], 1e-6)
        self.assertEqual(ev["octave_errors"], 0)
        self.assertEqual(ev["lock_time_s"], 0.0)
        self.assertEqual(ev["lag_s"], 0.0)

    def test_lag_is_the_shift_that_explains_the_track(self):
        # estimate(t) = truth(t - 40 ms): the reported lag must be 40 ms (0.5 ms grid), and once it is removed
        # the remaining error is zero.
        ev = track.evaluate(self._track(_truth_glide(self.T - 0.040)), _truth_glide, onset_t=0.0)
        self.assertAlmostEqual(ev["lag_s"], 0.040, delta=1e-4)
        self.assertLess(ev["cents_median"], 0.01)

    def test_lag_is_zero_when_the_truth_does_not_move(self):
        ev = track.evaluate(self._track(np.full_like(self.T, 55.0)), _const(55.0), onset_t=0.0)
        self.assertEqual(ev["lag_s"], 0.0)

    def test_octave_errors_are_counted(self):
        f = np.full_like(self.T, 55.0)
        f[100:110] = 110.0  # ten frames an octave too high, long after lock
        ev = track.evaluate(self._track(f), _const(55.0), onset_t=0.0)
        self.assertEqual(ev["octave_errors"], 10)
        self.assertEqual(ev["lock_time_s"], 0.0)
        self.assertLess(ev["cents_median"], 1e-6)  # the median ignores ten outliers

    def test_octave_errors_ignore_frames_the_gate_would_close(self):
        f = np.full_like(self.T, 55.0)
        f[100:110] = 110.0
        conf = np.ones_like(self.T)
        conf[100:110] = 0.2
        ev = track.evaluate(track.Track(self.T, f, conf), _const(55.0), onset_t=0.0)
        self.assertEqual(ev["octave_errors"], 0)

    def test_lock_time_is_the_first_run_of_three_good_frames(self):
        f = np.full_like(self.T, 55.0)
        f[:15] = np.nan  # no estimate for 30 ms
        f[20] = 82.5  # a bad frame later must not move the lock time
        ev = track.evaluate(self._track(f), _const(55.0), onset_t=0.0)
        self.assertAlmostEqual(ev["lock_time_s"], self.T[15], delta=1e-9)

    def test_lock_time_is_measured_from_the_onset(self):
        f = np.where(self.T < 0.12, 82.5, 55.0)
        ev = track.evaluate(self._track(f), _const(55.0), onset_t=0.1)
        self.assertAlmostEqual(ev["lock_time_s"], 0.02, delta=1e-9)

    def test_conf_open_time(self):
        conf = np.where(self.T < 0.05, 0.1, 0.9)
        ev = track.evaluate(track.Track(self.T, np.full_like(self.T, 55.0), conf), _const(55.0), onset_t=0.0)
        self.assertAlmostEqual(ev["conf_open_s"], 0.05, delta=1e-9)


class TestApplyPrior(unittest.TestCase):
    # The track is seeded by the first three consistent frames, so frames 0 and 1 are always NaN (no estimate).
    def test_single_octave_outlier_is_rejected(self):
        f = np.full(12, 55.0)
        f[6] = 110.0
        np.testing.assert_array_equal(track.apply_prior(f, np.ones(12))[2:], np.full(10, 55.0))

    def test_octave_jump_is_accepted_after_three_consistent_frames(self):
        f = np.concatenate([np.full(6, 55.0), np.full(6, 110.0)])
        out = track.apply_prior(f, np.ones(12))
        np.testing.assert_array_equal(out[2:8], np.full(6, 55.0))  # frames 6 and 7 are held
        np.testing.assert_array_equal(out[8:], np.full(4, 110.0))  # frame 8 is the third consistent one

    def test_disagreeing_outliers_never_accumulate(self):
        f = np.array([55, 55, 55, 110, 30, 110, 30, 110, 55, 55], dtype=float)
        np.testing.assert_array_equal(track.apply_prior(f, np.ones(10))[2:], np.full(8, 55.0))

    def test_smooth_glide_passes_untouched(self):
        f = 80.0 * 0.995 ** np.arange(100)  # 0.5 % per frame, far inside the +-25 % window
        np.testing.assert_array_equal(track.apply_prior(f, np.ones(100))[2:], f[2:])

    def test_new_note_after_signal_loss_is_taken_after_three_frames(self):
        f = np.concatenate([np.full(6, 55.0), np.full(6, 300.0), np.full(5, 110.0)])
        conf = np.concatenate([np.ones(6), np.full(6, 0.1), np.ones(5)])
        out = track.apply_prior(f, conf)
        np.testing.assert_array_equal(out[6:14], np.full(8, 55.0))  # unvoiced frames, then 2 unconfirmed ones, hold
        np.testing.assert_array_equal(out[14:], np.full(3, 110.0))  # five unvoiced frames forgot the track

    def test_first_estimate_needs_three_consistent_frames(self):
        # An isolated first frame (96 Hz at the start of a 185 -> 176 Hz glide) must not seed the track.
        f = np.array([96.0, 185.0, 182.0, 180.0, 178.0, 176.0])
        out = track.apply_prior(f, np.ones(6))
        self.assertTrue(np.all(np.isnan(out[:3])))  # nothing accepted yet: no estimate
        np.testing.assert_array_equal(out[3:], f[3:])


class TestDecimation(unittest.TestCase):
    def test_plan_is_integer_and_at_or_above_target(self):
        # 44.1 kHz / 2 kHz = 22.05: the plan takes D = 22 -> 2004.5 Hz rather than a fractional ratio.
        for fs in (22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000):
            d, fs_d = track.decimation_plan(fs, 2000.0)
            self.assertGreaterEqual(fs_d, 2000.0 - 1e-9, msg=f"fs {fs}")
            self.assertLessEqual(fs_d, 2100.0, msg=f"fs {fs}")
            self.assertAlmostEqual(d * fs_d, fs, places=6)
            self.assertEqual(d, int(d))
        self.assertEqual(track.decimation_plan(44100, 2000.0)[0], 22)
        self.assertEqual(track.decimation_plan(48000, 2000.0), (24, 2000.0))

    def test_detection_works_at_a_non_integer_ratio(self):
        fs = 44100
        trk = track.mpm(signals.sine(55.0, fs, 1.0, -12.0), fs)
        self.assertTrue(1500.0 <= trk.fs_d <= 2500.0)
        ev = track.evaluate(trk, _const(55.0), onset_t=0.0)
        self.assertLessEqual(ev["cents_median"], CENTS_0_5_PCT)

    def test_out_of_band_tone_does_not_alias_into_the_pitch_band(self):
        # 1900 Hz would fold to |1900 - 2000| = 100 Hz at fs_d = 2 kHz without the anti-alias filter.
        x = signals.sine(55.0, FS, 1.0, -12.0) + signals.sine(1900.0, FS, 1.0, -12.0)
        ev = track.evaluate(track.mpm(x, FS), _const(55.0), onset_t=0.0)
        self.assertLessEqual(ev["cents_median"], CENTS_0_5_PCT)
        self.assertEqual(ev["octave_errors"], 0)


class TestDetectors(unittest.TestCase):
    FREQS = (30.0, 41.2, 55.0, 82.4, 110.0, 164.8)

    def _sine_accuracy(self, detector):
        for fs in (44100, 48000):
            for f in self.FREQS:
                trk = detector(signals.sine(f, fs, 1.0, level_db=-12.0), fs)
                ev = track.evaluate(trk, _const(f), onset_t=0.0)
                self.assertLessEqual(ev["cents_median"], CENTS_0_5_PCT, msg=f"fs {fs} f {f}")
                self.assertEqual(ev["octave_errors"], 0, msg=f"fs {fs} f {f}")

    def test_mpm_sine_accuracy(self):
        self._sine_accuracy(track.mpm)

    def test_yin_sine_accuracy(self):
        self._sine_accuracy(track.yin)

    def test_frames_are_a_hop_apart_and_cover_the_signal(self):
        trk = track.mpm(signals.sine(55.0, FS, 1.0, -12.0), FS)
        self.assertLess(np.max(np.abs(np.diff(trk.t) - 0.002)), 1e-12)  # 4 samples at 2 kHz = 2 ms
        self.assertEqual(trk.t[0], 0.0)
        self.assertGreaterEqual(trk.t[-1], 1.0 - 0.002 - 1e-9)
        self.assertEqual(len(trk.f0), len(trk.t))
        self.assertEqual(len(trk.conf), len(trk.t))

    def test_window_length_follows_f_min_and_window_periods(self):
        x = signals.sine(55.0, FS, 0.5, -12.0)
        # W = ceil(2.5 * 2000 / 20) = 250 samples = 125 ms; W = ceil(3 * 2000 / 40) = 150 samples = 75 ms.
        self.assertAlmostEqual(track.mpm(x, FS).window_s, 0.125, delta=1e-9)
        self.assertAlmostEqual(track.mpm(x, FS, f_min=40.0, window_periods=3.0).window_s, 0.075, delta=1e-9)
        self.assertAlmostEqual(track.yin(x, FS, f_min=40.0, window_periods=3.0).window_s, 0.075, delta=1e-9)

    def test_octave_errors_808(self):
        # SIG-04: 808 notes E1 (MIDI 28) ... B2 (MIDI 47), octave glide from 2 f, 2nd harmonic at -8 dB.
        for midi in range(28, 48):
            f = _midi_hz(midi)
            x_b = band.range_filter(signals.bass_808(FS, f_end=f, glide_from=2.0 * f, harm2_db=-8.0), FS)
            ev = track.evaluate(track.mpm(x_b, FS, f_min=30.0), _truth_808(f, 2.0 * f), onset_t=0.0)
            self.assertEqual(ev["octave_errors"], 0, msg=f"note {midi} ({f:.1f} Hz)")
            self.assertLessEqual(ev["cents_median"], CENTS_0_5_PCT, msg=f"note {midi} ({f:.1f} Hz)")

    def test_glide_lag_bounded(self):
        # The estimate describes the middle of the window: W = ceil(2.5 * 2000 / 30) = 167 samples = 83 ms,
        # so about 42 ms of lag; the SPEC budget is 60 ms.
        ev = track.evaluate(track.mpm(_glide_signal(), FS, f_min=30.0), _truth_glide, onset_t=0.0)
        self.assertGreater(ev["lag_s"], 0.015)
        self.assertLess(ev["lag_s"], 0.060)

    def test_glide_lag_grows_with_the_window(self):
        lags = [track.evaluate(track.mpm(_glide_signal(), FS, f_min=30.0, window_periods=p), _truth_glide,
                               onset_t=0.0)["lag_s"] for p in (1.5, 3.0)]
        self.assertLess(lags[0], lags[1])

    def test_noise_low_confidence(self):
        # Band-limited pink noise is autocorrelated, so its raw NSDF clarity is 0.45-0.56 (measured): the
        # confidence must be calibrated so that noise stays below the gate while a sine is at 1.
        x = signals.pink_noise(FS, 3.0, 30.0, 120.0, level_db=-12.0, seed=1)
        for window_periods in (2.5, 1.5):
            trk = track.mpm(x, FS, f_min=30.0, window_periods=window_periods)
            self.assertLess(np.median(trk.conf[len(trk.conf) // 4:]), 0.5, msg=f"{window_periods} periods")

    def test_sine_confidence_is_high(self):
        for detector in (track.mpm, track.yin):
            trk = detector(signals.sine(55.0, FS, 1.0, -12.0), FS)
            self.assertGreaterEqual(np.median(trk.conf[len(trk.conf) // 4:]), 0.95)

    def test_confidence_opens_within_about_two_periods_of_an_onset(self):
        # 55 Hz after digital silence: the note must span 2 periods (36.4 ms) before a lag of one period is
        # measurable on a window that excludes the silence; allow a hop and the filter edge.
        x = np.concatenate([np.zeros(int(0.2 * FS)), signals.sine(55.0, FS, 0.5, -12.0)])
        truth = lambda t: np.where(np.asarray(t) >= 0.2, 55.0, np.nan)
        ev = track.evaluate(track.mpm(x, FS), truth, onset_t=0.2)
        self.assertGreater(ev["conf_open_s"], 0.027)  # not before 1.5 periods
        self.assertLessEqual(ev["conf_open_s"], 0.045)
        self.assertLessEqual(ev["lock_time_s"], 0.045)
        self.assertEqual(ev["octave_errors"], 0)


class TestPll(unittest.TestCase):
    def test_pll_phase_error(self):
        # After convergence (bandwidth 5 Hz -> about 0.4 s) the sub phase phi_sub = phase/2 must satisfy
        # 2*phi_sub = phi_in (analytic-signal phase) within 15 degrees rms, at any level.
        for level_db in (-6.0, -40.0):
            x = signals.sine(55.0, FS, 3.0, level_db=level_db)
            f_hz, phase = track.pll(x, FS, f_init=60.0, bw_hz=5.0)
            phi_in = np.unwrap(np.angle(sps.hilbert(x)))
            err = np.angle(np.exp(1j * (2.0 * (phase / 2.0) - phi_in)))
            seg = slice(int(1.5 * FS), int(2.5 * FS))
            self.assertLessEqual(np.degrees(np.sqrt(np.mean(err[seg] ** 2))), 15.0, msg=f"{level_db} dB")
            self.assertLess(abs(np.mean(f_hz[seg]) - 55.0) / 55.0, 0.002, msg=f"{level_db} dB")


class TestOscillator(unittest.TestCase):
    def test_oscillator_shape_blend(self):
        n, f = 2 * FS, 27.5  # 55 whole cycles
        sine = track.oscillator(np.full(n, f), FS, 0.0)
        tri = track.oscillator(np.full(n, f), FS, 100.0)
        mid = track.oscillator(np.full(n, f), FS, 50.0)
        self.assertLessEqual(util.thd_db(sine, FS, f), -80.0)
        # Triangle: harmonic k (odd) has amplitude 8/(pi^2 k^2) -> the 3rd is 1/9 of the fundamental (-19.08 dB).
        a = util.harmonic_amps(tri, FS, f, n=5)
        self.assertAlmostEqual(20.0 * np.log10(a[2] / a[0]), -19.08, delta=0.1)
        self.assertLess(a[1] / a[0], 1e-4)  # no even harmonics
        self.assertAlmostEqual(np.max(np.abs(tri)), 1.0, delta=0.002)
        np.testing.assert_allclose(mid, 0.5 * (sine + tri), atol=1e-12)

    def test_phase0_and_frequency(self):
        x = track.oscillator(np.full(FS, 40.0), FS, 0.0, phase0=np.pi / 2.0)
        self.assertAlmostEqual(x[0], 1.0, places=12)  # sin(pi/2)
        self.assertLess(abs(util.zc_freq(x, FS) - 40.0) / 40.0, 1e-5)

    def test_follows_a_time_varying_frequency(self):
        # f[n] = 30 + 30 n/fs: phase 2*pi*(30 t + 15 t^2) -> 44 rising crossings in 1 s (the 45th falls just
        # after the last sample); crossing 22 where 30 t + 15 t^2 = 22, i.e. t = (sqrt(2220) - 30)/30 s.
        f = 30.0 + 30.0 * np.arange(FS) / FS
        x = track.oscillator(f, FS, 0.0)
        z = util.zero_crossings(x, rising=True)
        self.assertEqual(len(z), 44)
        self.assertLess(abs(z[21] - (np.sqrt(2220.0) - 30.0) / 30.0 * FS), 0.5)  # rectangle-rule phase: < 0.2 sample


class TestF0PerSample(unittest.TestCase):
    T = np.arange(0.0, 1.0, 0.002)

    def _log_glide_track(self):
        return track.Track(self.T, 80.0 * 2.0 ** (-self.T), np.ones_like(self.T))

    def test_log_interpolation_is_exact_for_a_log_glide(self):
        n = FS // 2
        f = track.f0_per_sample(self._log_glide_track(), n, FS)
        np.testing.assert_allclose(f, 80.0 * 2.0 ** (-np.arange(n) / FS), rtol=1e-12)

    def test_lookahead_reads_future_frames(self):
        n = FS // 2
        f = track.f0_per_sample(self._log_glide_track(), n, FS, lookahead_s=0.020)
        np.testing.assert_allclose(f, 80.0 * 2.0 ** (-(np.arange(n) / FS + 0.020)), rtol=1e-12)

    def test_hold_is_causal(self):
        trk = track.Track(np.array([0.0, 0.002, 0.004]), np.array([50.0, 60.0, 70.0]), np.ones(3))
        f = track.f0_per_sample(trk, 300, FS, interp="hold")  # 96 samples per frame at 48 kHz
        self.assertEqual(f[0], 50.0)
        self.assertEqual(f[95], 50.0)
        self.assertEqual(f[96], 60.0)
        self.assertEqual(f[191], 60.0)
        self.assertEqual(f[192], 70.0)
        self.assertEqual(f[-1], 70.0)

    def test_nan_frames_are_filled_from_the_nearest_valid_value(self):
        trk = track.Track(np.array([0.0, 0.002, 0.004, 0.006]), np.array([np.nan, 60.0, np.nan, 70.0]), np.ones(4))
        f = track.f0_per_sample(trk, 400, FS, interp="hold")
        self.assertEqual(f[0], 60.0)  # leading NaN: the first valid value
        self.assertEqual(f[200], 60.0)  # NaN in the middle: the previous valid value
        self.assertEqual(f[300], 70.0)


if __name__ == "__main__":
    unittest.main()
