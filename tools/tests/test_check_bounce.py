# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for tools/check_bounce.py: reading bounces and finding the tones, the sub and the onset."""
import math
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import check_bounce  # noqa: E402
import make_channel_id_session as session  # noqa: E402


def bounce(tmp, layout, edit, sample_format="float32", extensible=True):
    """The session's signal for `layout`, changed by edit(channels), written as a bounce would be."""
    channels = session.signals(layout, 1.75, 0.25)
    edit(channels)
    path = Path(tmp) / "bounce.wav"
    session.write_wav(path, channels, session.FS, session.mask(layout), sample_format, extensible)
    return check_bounce.read_wav(path)


class TestCheckBounce(unittest.TestCase):
    def test_finds_the_sub_in_the_channel_that_carries_it(self):
        def add_sub(channels):  # a 30 Hz sub at -30 dBFS in LFE, as out_mode LFE gives it
            lfe = session.LAYOUTS["5.1"].index("LFE")
            w = 2.0 * math.pi * 30.0 / session.FS
            for i in range(12000, len(channels[lfe])):
                channels[lfe][i] += 0.0316 * math.sin(w * i)

        with tempfile.TemporaryDirectory() as tmp:
            result = check_bounce.analyse(bounce(tmp, "5.1", add_sub), lead=0.25, window=0.5)
        with_sub = [c.speaker for c in result if c.sub_db >= check_bounce.SUB_FLOOR_DB]
        self.assertEqual(with_sub, ["LFE"])
        self.assertAlmostEqual(result[3].sub_db, -30.0, delta=0.2)
        self.assertEqual([c.tone for c in result], list(range(6)))  # the dry stays where it was

    def test_reports_a_moved_channel(self):
        def swap(channels):
            channels[4], channels[5] = channels[5], channels[4]

        with tempfile.TemporaryDirectory() as tmp:
            wav = bounce(tmp, "5.1", swap, "pcm24")
        text = check_bounce.report(wav, check_bounce.analyse(wav, lead=0.25, window=0.5), "5.1")
        self.assertIn("channel 4 carries source channel 5", text)
        self.assertIn("channel 5 carries source channel 4", text)

    def test_reads_plain_pcm16_and_times_a_late_onset(self):
        def delay(channels):  # 480 samples (10 ms) late on R: uncompensated latency
            channels[1] = [0.0] * 480 + channels[1][:-480]

        with tempfile.TemporaryDirectory() as tmp:
            wav = bounce(tmp, "5.0", delay, "pcm16", extensible=False)
        self.assertEqual(wav.channel_mask, 0)
        result = check_bounce.analyse(wav, lead=0.25, window=0.5)
        self.assertAlmostEqual(result[0].onset_ms, 250.0, delta=1.0)
        self.assertAlmostEqual(result[1].onset_ms, 260.0, delta=1.0)
        self.assertEqual(result[0].speaker, "?")
        self.assertAlmostEqual(result[2].tone_db, -20.0, delta=0.1)  # 16-bit keeps the levels

    def test_refuses_what_is_not_a_wav(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.wav"
            path.write_bytes(b"RF64" + bytes(40))
            with self.assertRaises(ValueError):
                check_bounce.read_wav(path)
            path.write_bytes(b"RIFF\x04\x00\x00\x00WAVE")
            with self.assertRaises(ValueError):
                check_bounce.read_wav(path)
            self.assertEqual(check_bounce.main([str(path)]), 1)


if __name__ == "__main__":
    unittest.main()
