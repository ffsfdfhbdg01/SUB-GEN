# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the phase tools (SPEC §7.7, §8.B): the all-pass Hilbert pair, rotation and Auto-Align.

First-order all-pass (p - s)/(p + s) has phase -2 atan(w/p); chain B (poles interleaved above those of chain A) leads
A by 90 degrees across the band. y = cos(theta) A + sin(theta) B then leads the input chain by theta, with the exact
amplitude |y| = sqrt(1 - sin(2 theta) sin(eps)) ~ 1 - (sin(2 theta)/2) eps for a pair error eps: +-0.05 dB needs
eps <= 0.66 degrees, so the rotation tests use a design that reaches 0.5 degrees.
"""
import unittest

import numpy as np
from scipy import signal as sps

from subgen_proto import align, hilbert, signals

F_LO, F_HI = 12.0, 600.0


def _phase_deg(y, fs, f, seconds=1.0):
    """Phase (deg) and amplitude of the component at f in the last `seconds` (a whole number of cycles for integer f)."""
    n = int(seconds * fs)
    seg = y[-n:]
    k = int(round(f * n / fs))
    spec = np.fft.rfft(seg)[k]
    return float(np.degrees(np.angle(spec))), float(2.0 * np.abs(spec) / n)


def _wrap(a):
    return (a + 180.0) % 360.0 - 180.0


class TestHilbertPair(unittest.TestCase):
    def test_hilbert_pair_error(self):
        n = hilbert.min_sections(1.0, F_LO, F_HI)
        self.assertLessEqual(n, 10)
        d = hilbert.design(n, F_LO, F_HI)
        self.assertLessEqual(d.max_err_deg, 1.0)
        f = np.geomspace(F_LO, F_HI, 400)
        err = hilbert.phase_difference_deg(d, f) - 90.0
        self.assertLessEqual(np.max(np.abs(err)), 1.0)
        self.assertEqual(len(d.poles_a), n)
        self.assertEqual(len(d.poles_b), n)
        self.assertTrue(np.all(d.poles_a > 0) and np.all(d.poles_b > 0))

    def test_frozen_pair_is_the_4_section_design(self):
        # the plug-in's pair (SPEC §7.7) is frozen at full precision for the golden data and the C++ constants
        d = hilbert.design(4, F_LO, F_HI)
        np.testing.assert_allclose(d.poles_a, hilbert.PAIR.poles_a, rtol=1e-9)
        np.testing.assert_allclose(d.poles_b, hilbert.PAIR.poles_b, rtol=1e-9)
        self.assertAlmostEqual(d.max_err_deg, hilbert.PAIR.max_err_deg, delta=1e-9)
        self.assertLessEqual(hilbert.PAIR.max_err_deg, 0.2)  # T-FILT-05

    def test_error_falls_with_the_order(self):
        errs = [hilbert.design(n, F_LO, F_HI).max_err_deg for n in (2, 3, 4)]
        self.assertGreater(errs[0], errs[1])
        self.assertGreater(errs[1], errs[2])
        self.assertLess(hilbert.design(hilbert.min_sections(0.5, F_LO, F_HI), F_LO, F_HI).max_err_deg, 0.5)

    def test_chain_is_all_pass_and_b_leads_a(self):
        fs = 48000
        d = hilbert.design(hilbert.min_sections(1.0, F_LO, F_HI), F_LO, F_HI)
        for f in (20.0, 60.0, 200.0, 500.0):
            x = signals.sine(f, fs, 3.0, level_db=-6.0)
            a, b = (hilbert.allpass_chain(x, p, fs) for p in (d.poles_a, d.poles_b))
            (pa, aa), (pb, ab), (_, ax) = _phase_deg(a, fs, f), _phase_deg(b, fs, f), _phase_deg(x, fs, f)
            self.assertAlmostEqual(aa / ax, 1.0, delta=1e-4, msg=f"{f} Hz")  # all-pass: unit magnitude
            self.assertAlmostEqual(ab / ax, 1.0, delta=1e-4, msg=f"{f} Hz")
            self.assertAlmostEqual(_wrap(pb - pa), 90.0, delta=1.1, msg=f"{f} Hz")  # B leads A by 90 degrees


class TestRotation(unittest.TestCase):
    def test_rotation_phase_and_gain(self):
        # T-PHS-02: rotation theta in 30-300 Hz within 1 degree, amplitude within 0.05 dB, at every sample rate
        d = hilbert.design(hilbert.min_sections(0.5, F_LO, F_HI), F_LO, F_HI)
        for fs in (44100, 48000, 96000):
            for f in (30.0, 100.0, 300.0):
                x = signals.sine(f, fs, 3.0, level_db=-6.0)
                p0, a0 = _phase_deg(hilbert.rotate(x, 0.0, d, fs), fs, f)
                for theta in (-180.0, -90.0, -45.0, 0.0, 45.0, 90.0, 180.0):
                    p, a = _phase_deg(hilbert.rotate(x, theta, d, fs), fs, f)
                    self.assertLessEqual(abs(_wrap(p - p0 - theta)), 1.0, msg=f"fs {fs} f {f} theta {theta}")
                    self.assertLessEqual(abs(20.0 * np.log10(a / a0)), 0.05, msg=f"fs {fs} f {f} theta {theta}")

    def test_theta_zero_is_chain_a(self):
        d = hilbert.design(4, F_LO, F_HI)
        x = np.random.default_rng(0).standard_normal(2000)
        np.testing.assert_allclose(hilbert.rotate(x, 0.0, d, 48000), hilbert.allpass_chain(x, d.poles_a, 48000), atol=1e-12)

    def test_polarity_exact(self):
        x = np.random.default_rng(1).standard_normal(1000)
        self.assertTrue(np.array_equal(x + hilbert.polarity(x, -1), np.zeros_like(x)))  # T-PHS-01
        self.assertTrue(np.array_equal(hilbert.polarity(x, 1), x))


def _scenario(phi_d, fs=48000, f=80.0, dur=2.0):
    t = np.arange(int(dur * fs)) / fs
    dry = np.sin(2 * np.pi * f * t) + 0.5 * np.sin(2 * np.pi * f / 2 * t + np.radians(phi_d))
    sub = np.sin(2 * np.pi * f / 2 * t)
    return dry, sub


class TestAutoAlign(unittest.TestCase):
    def test_auto_align_known_optimum(self):
        # sub = sin(w t) must be turned by phi_d to add up with the dry's w/2 component: the effective rotation
        # psi = theta + 180 (pol < 0) equals phi_d, reported with |theta| <= 90 (a polarity flip instead of +-180)
        for phi_d in (0.0, 60.0, 120.0, 180.0, -90.0):
            dry, sub = _scenario(phi_d)
            r = align.auto_align(dry, sub, 48000, (20.0, 120.0))
            psi = _wrap(r.rot_deg + (180.0 if r.pol < 0 else 0.0))
            self.assertLessEqual(abs(_wrap(psi - phi_d)), 15.0, msg=f"phi_d {phi_d}: {r}")
            self.assertLessEqual(abs(r.rot_deg), 90.0, msg=f"phi_d {phi_d}: {r}")
            self.assertGreater(r.confidence_db, 0.5, msg=f"phi_d {phi_d}: {r}")
        self.assertEqual(align.auto_align(*_scenario(180.0), 48000, (20.0, 120.0)).pol, -1)
        self.assertEqual(align.auto_align(*_scenario(0.0), 48000, (20.0, 120.0)).pol, 1)

    def test_gain_is_measured_against_the_untouched_sub(self):
        dry, sub = _scenario(180.0)  # the sub starts in antiphase: flipping it must gain energy
        r = align.auto_align(dry, sub, 48000, (20.0, 120.0))
        # 40 Hz amplitude |0.5 e^{j180} + 1| = 0.5 -> |0.5 + 1| = 1.5; 80 Hz unchanged: power 0.5 + 0.125 -> 0.5 + 1.125
        self.assertAlmostEqual(r.gain_db, 10.0 * np.log10((0.5 + 0.5 * 1.5 ** 2) / (0.5 + 0.5 * 0.5 ** 2)), delta=0.1)

    def test_auto_align_no_overlap_low_confidence(self):
        t = np.arange(96000) / 48000
        dry = np.sin(2 * np.pi * 80.0 * t)  # nothing at f/2: the sub cannot interfere with it
        sub = np.sin(2 * np.pi * 40.0 * t)
        r = align.auto_align(dry, sub, 48000, (20.0, 120.0))
        self.assertLess(r.confidence_db, 0.5)
        self.assertEqual((r.pol, r.rot_deg), (1, 0.0))  # no evidence: leave the sub alone

    def test_headroom_objective_reduces_peak(self):
        dry, sub = _scenario(60.0)
        r = align.auto_align(dry, sub, 48000, (20.0, 120.0), objective="headroom")
        a = sps.hilbert(sub)
        z = lambda pol, theta: dry + pol * np.real(a * np.exp(1j * np.radians(theta)))
        self.assertLess(np.max(np.abs(z(r.pol, r.rot_deg))), np.max(np.abs(z(1, 0.0))) - 1e-3)
        self.assertGreater(r.gain_db, 0.0)

    def test_objectives_differ(self):
        dry, sub = _scenario(60.0)
        e = align.auto_align(dry, sub, 48000, (20.0, 120.0), objective="energy")
        h = align.auto_align(dry, sub, 48000, (20.0, 120.0), objective="headroom")
        self.assertNotEqual((e.pol, e.rot_deg), (h.pol, h.rot_deg))  # adding up and keeping the peak low are opposite goals

    def test_step_parameter(self):
        dry, sub = _scenario(45.0)
        r = align.auto_align(dry, sub, 48000, (20.0, 120.0), step_deg=45)
        self.assertEqual((r.pol, r.rot_deg), (1, 45.0))
        with self.assertRaises(ValueError):
            align.auto_align(dry, sub, 48000, (20.0, 120.0), objective="loudness")


if __name__ == "__main__":
    unittest.main()
