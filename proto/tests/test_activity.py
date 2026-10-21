# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for S4 activity and note boundaries (SPEC §5.4, §6.2; D8 C2)."""
import unittest

import numpy as np

from subgen_proto import activity, band, signals

FS = 48000


def _floor(n, rms_db, seed=5):
    """Pink 30-120 Hz noise at the given RMS level."""
    nz = signals.pink_noise(FS, n / FS, 30.0, 120.0, level_db=0.0, seed=seed)[:n]
    return nz * (10.0 ** (rms_db / 20.0) / np.sqrt(np.mean(nz ** 2)))


def _hits(n_hits=20, spacing=0.5, t60=0.3, extra=None):
    x = signals.kick(FS, n_hits=n_hits, spacing=spacing, level_db=-6.0, t60=t60)
    if extra is not None:
        x = x + extra(len(x))
    return band.range_filter(x, FS)


class TestActivity(unittest.TestCase):
    def test_threshold_hysteresis_and_hold(self):
        # RMS -40 dBFS (on), then -57 dBFS (inside the 3 dB hysteresis: stays on), then -65 dBFS (off after the hold).
        seg = lambda rms_db: signals.sine(55.0, FS, 0.5, level_db=rms_db + 3.0103)
        x = np.concatenate([seg(-40.0), seg(-57.0), seg(-65.0)])
        a = activity.activity(x, FS)
        self.assertTrue(np.all(a[int(0.05 * FS):FS]))
        self.assertFalse(np.any(a[int(1.1 * FS):]))

    def test_empty_input(self):
        self.assertEqual(len(activity.activity(np.zeros(0), FS)), 0)
        self.assertEqual(len(activity.note_starts(np.zeros(0), FS)), 0)
        self.assertEqual(len(activity.note_ends(np.zeros(0), FS)), 0)


class TestNoteBoundaries(unittest.TestCase):
    def _assert_one_start_per_hit(self, x, spacing, tol_ms, label):
        starts = activity.note_starts(x, FS)
        self.assertEqual(len(starts), 20, msg=label)
        for i, (t, b) in enumerate(zip(starts.detect, starts.begin)):
            t0 = i * spacing * FS
            self.assertGreaterEqual(b, t0 - 1.0, msg=f"{label} hit {i}")  # where the rise began
            self.assertLessEqual(b, t0 + tol_ms * 1e-3 * FS, msg=f"{label} hit {i}")
            self.assertLessEqual(t - b, 0.010 * FS, msg=f"{label} hit {i}")  # detected within 10 ms of that

    def test_one_start_per_kick_from_silence(self):
        # the band filter (BW4 30-120 Hz) delays the detectable onset of x_b by ~1.4 ms
        self._assert_one_start_per_hit(_hits(), 0.5, 3.0, "silence")

    def test_starts_over_noise_floors_hum_and_long_tails(self):
        hum = lambda n: 10.0 ** (-70.0 / 20.0) * np.sin(2.0 * np.pi * 31.0 * np.arange(n) / FS)
        # Over a tail 13.5 dB down, the new hit's band-limited envelope needs ~3.4 ms to rise above the tail.
        for label, x, spacing, tol in (
                ("pink -90", _hits(extra=lambda n: _floor(n, -90.0)), 0.5, 3.0),
                ("pink -60", _hits(extra=lambda n: _floor(n, -60.0)), 0.5, 3.0),
                ("hum -70", _hits(extra=hum), 0.5, 3.0),
                ("T60 1 s every 0.45 s", _hits(spacing=0.45, t60=1.0), 0.45, 5.0)):
            self._assert_one_start_per_hit(x, spacing, tol, label)

    def test_note_ends_between_separated_hits(self):
        ends = activity.note_ends(_hits(), FS)
        self.assertEqual(len(ends), 20)
        for i, e in enumerate(ends):
            self.assertTrue(i * 0.5 * FS < e < (i + 1) * 0.5 * FS, msg=f"hit {i}")

    def test_a_sustained_note_starts_once(self):
        x = band.range_filter(signals.bass_808(FS, f_end=45.0, glide_from=90.0, level_db=-6.0), FS)
        self.assertEqual(len(activity.note_starts(x, FS)), 1)

    def test_random_inband_noise_rarely_starts_a_note(self):
        for seed in (4, 5, 6, 7, 8):
            x = signals.pink_noise(FS, 3.0, 30.0, 120.0, level_db=-12.0, seed=seed)
            self.assertLessEqual(len(activity.note_starts(x, FS)), 3, msg=f"seed {seed}")


if __name__ == "__main__":
    unittest.main()
