# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the rendering helpers (loudness-matched A/B files, 5.1 LFE layout) and the engine wrappers used by the
listening renders and render_file."""
import tempfile
import unittest
from pathlib import Path

import numpy as np
import soundfile as sf

from subgen_proto import render, signals, util

FS = 48000


class TestLoudness(unittest.TestCase):
    def test_gain_match(self):
        ref = signals.pink_noise(FS, 2.0, level_db=-20.0)
        test = signals.sine(80.0, FS, 2.0, level_db=-6.0)
        matched, gain = render.gain_match(ref, test, FS)
        self.assertLess(abs(util.loudness_a_db(matched, FS) - util.loudness_a_db(ref, FS)), 0.1)
        np.testing.assert_allclose(matched, test * 10 ** (gain / 20.0), rtol=1e-12)

    def test_gain_match_stereo(self):
        ref = np.stack([signals.sine(100.0, FS, 1.0, -10.0), signals.sine(100.0, FS, 1.0, -16.0)], axis=1)
        test = np.stack([signals.sine(100.0, FS, 1.0, -30.0)] * 2, axis=1)
        matched, gain = render.gain_match(ref, test, FS)
        self.assertLess(abs(render.loudness_db(matched, FS) - render.loudness_db(ref, FS)), 0.1)


class TestFiles(unittest.TestCase):
    def test_write_ab_files(self):
        dry = signals.kick(FS, n_hits=2, spacing=0.5, level_db=-6.0)
        wet = {"divide": dry + 0.5 * signals.sine(30.0, FS, 1.0, -12.0), "trigger": 1.8 * dry}
        with tempfile.TemporaryDirectory() as tmp:
            paths = render.write_ab(tmp, "uc_test", dry, wet, FS)
            names = sorted(p.name for p in paths)
            self.assertEqual(names, ["uc_test_A_dry.wav", "uc_test_B_divide.wav", "uc_test_B_trigger.wav"])
            loud = {}
            for p in paths:
                info = sf.info(p)
                self.assertEqual(info.subtype, "PCM_24")
                self.assertEqual(info.samplerate, FS)
                self.assertEqual(info.frames, len(dry))
                data, _ = sf.read(p)
                self.assertLessEqual(np.max(np.abs(data)), 1.0)
                loud[p.name] = util.loudness_a_db(data, FS)
            for name in ("uc_test_B_divide.wav", "uc_test_B_trigger.wav"):
                self.assertLess(abs(loud[name] - loud["uc_test_A_dry.wav"]), 0.1, msg=name)

    def test_lfe_51_channel_layout(self):
        # WAV order FL FR FC LFE BL BR; the LFE channel is band-limited by the LFE low-pass (BW24 at 120 Hz). The
        # input adds a 5 ms broadband burst to the kick so that the low-pass has real work to do.
        kick = signals.kick(FS, n_hits=2, spacing=0.5, level_db=-6.0)
        burst = np.zeros_like(kick)
        burst[:240] = 0.3 * np.random.default_rng(0).standard_normal(240)
        out = render.lfe_51(kick, kick + burst, FS)
        self.assertEqual(out.shape, (len(kick), 6))
        np.testing.assert_array_equal(out[:, 0], kick)
        np.testing.assert_array_equal(out[:, 1], kick)
        for ch in (2, 4, 5):
            self.assertTrue(np.all(out[:, ch] == 0.0))
        lfe = out[:, 3]
        low, high = util.band_db(lfe, FS, 0.0, 120.0), util.band_db(lfe, FS, 240.0, FS / 2)
        self.assertGreaterEqual(low - high, 40.0)
        self.assertGreater(util.band_db(kick + burst, FS, 240.0, FS / 2) - high, 20.0)  # the low-pass removed it

    def test_write_multichannel(self):
        out = render.lfe_51(signals.kick(FS, n_hits=1, spacing=0.5), signals.kick(FS, n_hits=1, spacing=0.5), FS)
        with tempfile.TemporaryDirectory() as tmp:
            p = render.write_wav(Path(tmp) / "lfe.wav", out, FS)
            info = sf.info(p)
            self.assertEqual((info.channels, info.subtype), (6, "PCM_24"))


class TestEngines(unittest.TestCase):
    def _onset_sine(self, f=55.0):
        return np.concatenate([np.zeros(FS // 2), signals.sine(f, FS, 1.5, level_db=-12.0)])

    def test_track_sub_is_half_the_frequency(self):
        y = render.track_sub(self._onset_sine(), FS)
        self.assertLess(abs(util.zc_freq(y[FS:], FS) - 27.5) / 27.5, 0.005)

    def test_track_sub_lookahead_starts_with_the_note_not_before(self):
        x = self._onset_sine()
        on = FS // 2
        ahead = render.track_sub(x, FS, lookahead_s=0.045)
        live = render.track_sub(x, FS, lookahead_s=0.0)
        steady = util.rms(ahead[on + FS // 2:on + FS])
        self.assertLess(util.rms(ahead[:on]), 1e-6 * steady)  # no pre-echo: the amplitude follows the source
        early = slice(on + int(0.005 * FS), on + int(0.025 * FS))
        self.assertGreater(util.rms(ahead[early]), 0.3 * steady)  # with lookahead the pitch is known at the onset
        self.assertLess(util.rms(live[early]), 0.05 * steady)  # without it the gate is still shut (~2 periods)

    def test_divide_sub_gates_noise_and_passes_a_tone(self):
        # random in-band noise opens the periodicity gate now and then (Task 3 ruling on T-DIV-05: open <= 3 % of the
        # time, sub at least 12 dB below the noise; seed 4 is the worst measured case)
        noise = signals.pink_noise(FS, 2.0, 30.0, 120.0, level_db=-12.0, seed=4)
        self.assertLess(util.rms(render.divide_sub(noise, FS)[FS // 2:]), 10 ** (-12.0 / 20.0) * util.rms(noise))
        tone = signals.sine(55.0, FS, 2.0, level_db=-12.0)
        self.assertLess(abs(util.zc_freq(render.divide_sub(tone, FS)[FS:], FS) - 27.5) / 27.5, 0.005)

    def test_engine_sub_dispatch(self):
        x = self._onset_sine()
        for engine in ("divide", "track", "trigger", "harmonic"):
            y = render.engine_sub(x, FS, engine)
            self.assertEqual(len(y), len(x), msg=engine)
            self.assertTrue(np.all(np.isfinite(y)), msg=engine)
            self.assertGreater(util.rms(y), 0.0, msg=engine)
        with self.assertRaises(ValueError):
            render.engine_sub(x, FS, "spectral")


class TestRenderFileCli(unittest.TestCase):
    def test_render_file_adds_a_sub_and_keeps_the_format(self):
        from experiments import render_file
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "in.wav"
            x = np.stack([signals.kick(FS, n_hits=2, spacing=0.5, level_db=-6.0)] * 2, axis=1)
            sf.write(src, x, FS, subtype="PCM_24")
            out = Path(tmp) / "out.wav"
            render_file.main([str(src), "--engine", "trigger", "--out", str(out)])
            y, fs = sf.read(out)
            self.assertEqual((fs, y.shape, sf.info(out).subtype), (FS, x.shape, "PCM_24"))
            self.assertGreater(util.band_db(y[:, 0], FS, 20.0, 100.0), util.band_db(x[:, 0], FS, 20.0, 100.0) + 1.0)
            self.assertLessEqual(np.max(np.abs(y)), 1.0)

    def test_render_file_wet_only_is_band_limited(self):
        from experiments import render_file
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "in.wav"
            sf.write(src, signals.bass_808(FS), FS, subtype="PCM_24")
            out = Path(tmp) / "wet.wav"
            render_file.main([str(src), "--engine", "divide", "--mode", "wet", "--drive", "50", "--out", str(out)])
            y, _ = sf.read(out)
            self.assertEqual(y.ndim, 1)
            self.assertLess(util.band_db(y, FS, 400.0, 20000.0), util.band_db(y, FS, 15.0, 120.0) - 30.0)

    def test_render_file_rejects_an_unknown_engine(self):
        from experiments import render_file
        with self.assertRaises(SystemExit):
            render_file.main(["in.wav", "--engine", "spectral"])


if __name__ == "__main__":
    unittest.main()
