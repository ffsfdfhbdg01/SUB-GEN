# SPDX-License-Identifier: AGPL-3.0-or-later
"""Degenerate inputs (D8 M6): empty buffers, buffers shorter than a lookahead, silence."""
import unittest

import numpy as np

from subgen_proto import align, drive, dynamics, harmonic, render, track, trigger

FS = 48000


class TestDegenerateInputs(unittest.TestCase):
    def test_empty_inputs_give_empty_outputs(self):
        z = np.zeros(0)
        self.assertEqual(len(track.mpm(z, FS).f0), 0)
        self.assertEqual(len(render.track_sub(z, FS)), 0)
        self.assertEqual(len(render.divide_sub(z, FS)), 0)
        self.assertEqual(len(drive.process(z, FS, drive_pct=50.0, mode="soft")), 0)
        self.assertEqual(len(drive.process(z, FS, drive_pct=50.0, mode="hard", os_factor=2)), 0)
        self.assertEqual(len(harmonic.harmonic(z, FS)), 0)
        self.assertEqual(len(dynamics.limiter(z, FS, ceiling_db=-1.0, lookahead_ms=5.0, release_ms=100.0)), 0)
        self.assertEqual(len(trigger.render(z, FS)), 0)

    def test_limiter_shorter_than_its_lookahead(self):
        y = dynamics.limiter(np.full(10, 2.0), FS, ceiling_db=-1.0, lookahead_ms=5.0, release_ms=100.0)
        self.assertEqual(len(y), 10)
        self.assertTrue(np.all(y == 0.0))  # everything is delayed past the end

    def test_auto_align_on_silence_has_no_clear_optimum(self):
        z = np.zeros(4800)
        r = align.auto_align(z, z, FS, (20.0, 120.0))  # the suite runs with -W error: no log10(0)
        self.assertEqual((r.pol, r.rot_deg, r.confidence_db), (1, 0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
