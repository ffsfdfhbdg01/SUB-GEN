# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the LFE filter analysis and the linear-phase FIR (SPEC §7.6, SPEC_IO_UI §4).

Butterworth magnitude |H|^2 = 1/(1 + r^(2n)) gives 10 log10(1 + r^(2n)) dB at r = f/fc; at r = 1.5 that is 7.83 / 14.25 /
28.18 / 56.35 dB for n = 2 / 4 / 8 / 16 (12 / 24 / 48 / 96 dB/oct). Linkwitz-Riley of order 2n is two Butterworth n filters.
"""
import unittest

import numpy as np
from scipy import signal as sps

from subgen_proto import lfe, svf

FS = 48000


def _target(fs, hp=20.0, lp=120.0):
    secs = svf.design_butter(4, hp, fs, "high") + svf.design_butter(4, lp, fs, "low")
    return lambda f: np.abs(svf.freqz(secs, f, fs))


def _fir_response(h, fs, f):
    return np.abs(np.fft.rfft(h, 2 ** 19))[np.round(np.asarray(f) / fs * 2 ** 19).astype(int)]


class TestAnalysis(unittest.TestCase):
    def test_group_delay_matches_scipy(self):
        # BW4 LP 120 Hz, 10-200 Hz: the SVF analysis against SciPy's own group delay of the equivalent biquads
        sections = svf.design_butter(4, 120.0, FS, "low")
        f = np.linspace(10.0, 200.0, 60)
        gd = svf.group_delay_s(sections, f, FS)
        b, a = sps.sos2tf(svf.to_sos(sections))
        _, ref = sps.group_delay((b, a), w=f, fs=FS)
        self.assertLess(np.max(np.abs(gd / (ref / FS) - 1.0)), 0.005)

    def test_out_of_band_attenuation(self):
        for order, want in ((2, 7.83), (4, 14.25), (8, 28.18), (16, 56.35)):
            got = lfe.out_of_band_db("butter", order, 120.0, FS)
            self.assertAlmostEqual(got, 10.0 * np.log10(1.0 + 1.5 ** (2 * order)), delta=0.01, msg=f"order {order}")
            self.assertAlmostEqual(got, want, delta=0.1, msg=f"order {order}")
        # the brief's lower bounds (7.0 / 14.2 / 28.1 / 56.3 dB) hold as well
        self.assertTrue(all(lfe.out_of_band_db("butter", o, 120.0, FS) >= b for o, b in ((2, 7.0), (4, 14.2), (8, 28.1), (16, 56.3))))

    def test_out_of_band_linkwitz_riley_and_bessel(self):
        self.assertAlmostEqual(lfe.out_of_band_db("lr", 4, 120.0, FS), 2.0 * 7.83, delta=0.1)  # LR4 = BW2 x BW2
        self.assertAlmostEqual(lfe.out_of_band_db("lr", 8, 120.0, FS), 2.0 * 14.25, delta=0.1)
        b4, bes4 = lfe.out_of_band_db("butter", 4, 120.0, FS), lfe.out_of_band_db("bessel", 4, 120.0, FS)
        self.assertGreater(bes4, 3.0)
        self.assertLess(bes4, b4)  # Bessel rolls off more gently
        self.assertAlmostEqual(lfe.out_of_band_db("butter", 4, 120.0, FS, ratio=2.0), 10.0 * np.log10(1.0 + 2.0 ** 8), delta=0.1)

    def test_encoder_filter(self):
        # ATSC A/52 names an 8th-order elliptic low-pass at 120 Hz; 0.1 dB / 80 dB are an assumption (see the module)
        for fs in (44100, 48000, 96000):
            sos = lfe.encoder_filter_sos(fs)
            self.assertEqual(sos.shape, (4, 6))
            f = np.linspace(10.0, 120.0, 200)
            mag = 20.0 * np.log10(np.abs(sps.sosfreqz(sos, worN=f, fs=fs)[1]))
            self.assertLess(np.max(np.abs(mag)), 0.1 + 1e-3, msg=f"fs {fs}")  # passband ripple
            self.assertGreater(-20.0 * np.log10(np.abs(sps.sosfreqz(sos, worN=[200.0], fs=fs)[1][0])), 79.9, msg=f"fs {fs}")

    def test_group_delay_of_sos_matches_the_phase_slope(self):
        sos = lfe.encoder_filter_sos(FS)
        f0, df = 40.0, 1e-3
        h = sps.sosfreqz(sos, worN=[f0 - df, f0 + df], fs=FS)[1]
        slope = -np.angle(h[1] / h[0]) / (2.0 * np.pi * 2.0 * df)  # -dphi/dw by a central difference, seconds
        self.assertAlmostEqual(float(lfe.group_delay_sos_s(sos, [f0], FS)[0]), slope, delta=1e-7)

    def test_cascade_with_encoder(self):
        # group delays add: LFE low-pass (BW4 at 100 Hz) followed by the encoder's elliptic low-pass, at 40 Hz
        sections = svf.design_butter(4, 100.0, FS, "low")
        sos = lfe.encoder_filter_sos(FS)
        f0, df = 40.0, 1e-3
        fq = np.array([f0 - df, f0 + df])
        h = svf.freqz(sections, fq, FS) * sps.sosfreqz(sos, worN=fq, fs=FS)[1]
        measured = -np.angle(h[1] / h[0]) / (2.0 * np.pi * 2.0 * df)
        parts = float(svf.group_delay_s(sections, [f0], FS)[0]) + float(lfe.group_delay_sos_s(sos, [f0], FS)[0])
        self.assertAlmostEqual(measured, parts, delta=1e-7)  # a few nanoseconds: the finite-difference truncation
        self.assertGreater(parts, 3e-3)


class TestLinearPhaseFir(unittest.TestCase):
    def test_fir_symmetric_and_error(self):
        # T-FILT-06: HP 20 Hz x LP 120 Hz (BW4 each), 16384 taps at 48 kHz
        h = lfe.linear_phase_fir(_target(FS), FS, 16384)
        self.assertEqual(len(h), 16384)
        self.assertTrue(np.array_equal(h, h[::-1]))  # exactly symmetric
        f = np.geomspace(25.0, 2000.0, 400)
        err = 20.0 * np.log10(_fir_response(h, FS, f) / _target(FS)(f))
        self.assertLessEqual(np.max(np.abs(err)), 0.1)
        # delay (N - 1)/2 samples: the slope of the unwrapped phase in the pass band
        k = np.round(np.array([40.0, 80.0]) / FS * 2 ** 19).astype(int)
        phase = np.unwrap(np.angle(np.fft.rfft(h, 2 ** 19)[k[0]:k[1] + 1]))
        delay = -(phase[-1] - phase[0]) / (2.0 * np.pi * (k[1] - k[0]) / 2 ** 19)
        self.assertAlmostEqual(delay, (16384 - 1) / 2.0, delta=0.01)

    def test_target_is_the_amplitude_not_its_square(self):
        # |H| of the prototype is -3.01 dB at its corners; a kernel built from |H|^2 would sit at -6.02 dB there
        h = lfe.linear_phase_fir(_target(FS, hp=20.0, lp=2000.0), FS, 16384)
        # (20 Hz lies below the 25 Hz where the 0.1 dB criterion starts: the window smear costs ~0.17 dB there;
        # the point is -3 dB against -6 dB)
        self.assertAlmostEqual(20.0 * np.log10(_fir_response(h, FS, [20.0])[0]), -3.01, delta=0.5)

    def test_odd_length_has_an_integer_delay(self):
        h = lfe.linear_phase_fir(_target(FS), FS, 4097)
        self.assertTrue(np.array_equal(h, h[::-1]))
        self.assertEqual(int(np.argmax(np.abs(h))), 2048)  # (N - 1)/2

    def test_min_taps_monotonic(self):
        n20 = lfe.min_taps(_target(FS, hp=20.0), FS)
        n40 = lfe.min_taps(_target(FS, hp=40.0), FS, f_check=(50.0, 2000.0))
        self.assertGreater(n20, n40)  # a narrower corner needs a longer kernel
        n96 = lfe.min_taps(_target(96000, hp=20.0), 96000)
        self.assertGreater(n96, n20)  # the same time resolution needs twice the samples at twice the rate
        # and min_taps really is the boundary: it meets the tolerance, 5 % fewer taps do not
        f = np.geomspace(25.0, 2000.0, 300)
        meets = lambda n: np.max(np.abs(20.0 * np.log10(_fir_response(lfe.linear_phase_fir(_target(FS), FS, n), FS, f) / _target(FS)(f)))) <= 0.1
        self.assertTrue(meets(n20))
        self.assertFalse(meets(int(n20 * 0.95)))


if __name__ == "__main__":
    unittest.main()
