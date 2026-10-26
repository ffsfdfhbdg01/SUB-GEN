# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for tools/make_channel_id_session.py: SIG-15 per layout, written as WAVE_FORMAT_EXTENSIBLE."""
import math
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import check_bounce  # noqa: E402
import make_channel_id_session as session  # noqa: E402


class TestSession(unittest.TestCase):
    def test_each_channel_carries_its_own_tone_and_its_class_bed(self):
        with tempfile.TemporaryDirectory() as tmp:
            for layout, speakers in session.LAYOUTS.items():
                with self.subTest(layout=layout):
                    path = Path(tmp) / f"{layout}.wav"
                    session.write_wav(path, session.signals(layout, 1.75, 0.25), session.FS, session.mask(layout))
                    wav = check_bounce.read_wav(path)
                    self.assertEqual(wav.fs, 48000)
                    self.assertEqual(wav.channel_mask, session.mask(layout))
                    self.assertEqual(check_bounce.speakers(wav.channel_mask, len(wav.channels)), speakers)
                    for k, c in enumerate(check_bounce.analyse(wav, lead=0.25, window=0.5)):
                        bed = 20.0 * math.log10(session.BED[session.SPEAKERS[speakers[k]][1]])
                        self.assertEqual(c.tone, k)
                        self.assertAlmostEqual(c.tone_db, -20.0, delta=0.1)
                        self.assertAlmostEqual(c.bed_db, bed, delta=0.1)
                        self.assertLess(c.sub_db, -80.0)
                        self.assertAlmostEqual(c.onset_ms, 250.0, delta=1.0)

    def test_the_channel_masks_follow_the_wav_order(self):
        # a WAV interleaves its channels by ascending mask bit: the layouts list them that way
        for layout, speakers in session.LAYOUTS.items():
            bits = [session.SPEAKERS[s][0] for s in speakers]
            self.assertEqual(bits, sorted(bits), layout)
        self.assertEqual(session.mask("5.1"), 0x60F)
        self.assertEqual(session.mask("7.1.4"), 0x2D63F)


if __name__ == "__main__":
    unittest.main()
