# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the TRIGGER engine (SPEC §6.4). The voice formulas are transcribed here independently:
phase(t) = phi0 + 2 pi [f_end t + (f_start - f_end) tau_p (1 - exp(-t/tau_p))],  a(t) = t/t_a (t < t_a), then
10^(-3 (t - t_a)/T60);  Sub -2 has the same envelope and half the frequency (phase phi0 + (phase - phi0)/2).

Click metric: the largest second difference |y[n] - 2 y[n-1] + y[n-2]| in a +-1 ms window around the retrigger
(a step of size s shows up as s; a clean 80 Hz voice has (2 pi 80 / fs)^2 = 1.1e-4). The linear attack ramp has a kink
at the end of the attack, which is why the window is local.
"""
import unittest

import numpy as np
from scipy import signal as sps

from subgen_proto import envelope, signals, trigger, util

FS = 48000


def _formula(fs, n, *, f_start=80.0, f_end=45.0, tau_p_ms=30.0, attack_ms=2.0, t60_ms=350.0, phase0_deg=0.0, frac=0.0):
    t = (np.arange(n) - frac) / fs
    tt = np.maximum(t, 0.0)
    tau, ta, phi0 = tau_p_ms * 1e-3, max(attack_ms, 0.5) * 1e-3, np.radians(phase0_deg)  # attack >= 0.5 ms (D8 M5)
    phase = phi0 + 2.0 * np.pi * (f_end * tt + (f_start - f_end) * tau * (1.0 - np.exp(-tt / tau)))
    ramp = np.divide(tt, ta, out=np.zeros_like(tt), where=ta > 0.0)
    a = np.where(tt < ta, ramp, 10.0 ** (-3.0 * (tt - ta) / (t60_ms * 1e-3)))
    on = t >= 0.0
    return (np.where(on, a * np.sin(phase), 0.0), np.where(on, a * np.sin(phi0 + (phase - phi0) / 2.0), 0.0))


def _click(y, centre, half_ms=1.0):
    h = int(half_ms * 1e-3 * FS)
    seg = y[int(centre) - h:int(centre) + h + 1]
    return float(np.max(np.abs(np.diff(seg, 2))))


def _bursts(n_bursts, spacing_s, amp=0.05, length_ms=10.0, fs=FS):
    """100 Hz sine bursts (start at zero phase) of the given amplitude, one every `spacing_s`."""
    x = np.zeros(int(round(n_bursts * spacing_s * fs)))
    n = int(length_ms * 1e-3 * fs)
    burst = amp * np.sin(2.0 * np.pi * 100.0 * np.arange(n) / fs)
    for k in range(n_bursts):
        s = int(round(k * spacing_s * fs))
        x[s:s + n] = burst
    return x


class TestDetectOnsets(unittest.TestCase):
    def test_step_crossing_matches_the_discrete_exponential(self):
        # |x| steps to A = 0.2 at sample n0. The follower (attack 0.2 ms) then gives e[n0 + j] = A (1 - r^(j+1)),
        # r = exp(-1/(0.2e-3 fs)); theta = 10^(-30/20). The crossing lies between samples j and j + 1 and the
        # reported index interpolates linearly (SPEC: "interpolacja liniowa przecięcia").
        n0, amp = 4800, 0.2
        x = np.zeros(FS // 2)
        x[n0:] = amp
        r = np.exp(-1.0 / (0.2e-3 * FS))
        theta = 10.0 ** (-30.0 / 20.0)
        e = lambda j: amp * (1.0 - r ** (j + 1))
        j = next(j for j in range(50) if e(j) < theta <= e(j + 1))
        expected = n0 + j + (theta - e(j)) / (e(j + 1) - e(j))
        onsets = trigger.detect_onsets(x, FS)
        self.assertEqual(len(onsets), 1)
        self.assertAlmostEqual(onsets[0], expected, delta=1e-6)

    def test_kick_onsets_match_the_continuous_envelope_crossing(self):
        # Reference: the same kick evaluated at 64x the rate, the same follower there (a 64x finer integration of
        # the continuous one-pole), crossing found by linear interpolation on the fine grid.
        x = signals.kick(FS, n_hits=4, spacing=0.5, level_db=-6.0)
        onsets = trigger.detect_onsets(x, FS)
        self.assertEqual(len(onsets), 4)  # one per hit: no double triggers
        xf = signals.kick(64 * FS, n_hits=1, spacing=0.02, level_db=-6.0)
        ef = envelope.peak_follower(xf, 64 * FS, 0.2, 15.0)
        theta = 10.0 ** (-30.0 / 20.0)
        i = int(np.argmax(ef >= theta))
        t_ref = ((i - 1) + (theta - ef[i - 1]) / (ef[i] - ef[i - 1])) / 64.0  # samples at FS
        for k, t in enumerate(onsets):
            self.assertLessEqual(abs(t - (k * int(0.5 * FS) + t_ref)), 1.0, msg=f"hit {k}")  # T-TRIG-01: <= 1 sample

    def test_live_detection_is_within_2_ms_of_the_hit(self):
        x = signals.kick(FS, n_hits=4, spacing=0.5, level_db=-6.0)
        for k, t in enumerate(trigger.detect_onsets(x, FS)):
            delay = t - k * int(0.5 * FS)
            self.assertGreaterEqual(delay, 0.0, msg=f"hit {k}")
            self.assertLessEqual(delay, 0.002 * FS, msg=f"hit {k}")  # T-TRIG-01 Live

    def test_lockout_blocks_close_hits(self):
        x = _bursts(8, 0.030)  # a burst every 30 ms; the envelope falls below theta - 3 dB in every gap
        every = trigger.detect_onsets(x, FS, hold_ms=0.0, lockout_ms=20.0)
        self.assertEqual(len(every), 8)
        on = trigger.detect_onsets(x, FS, hold_ms=0.0, lockout_ms=50.0)
        self.assertEqual(len(on), 4)  # bursts 0, 2, 4, 6: the odd ones come 30 ms after an event
        self.assertTrue(np.all(np.diff(on) >= 0.050 * FS))
        on60 = trigger.detect_onsets(x, FS, hold_ms=0.0, lockout_ms=60.0)
        self.assertGreater(on60[1] - on60[0], 0.030 * FS * 1.5)  # the second hit is ignored
        self.assertTrue(np.all(np.diff(on60) >= 0.060 * FS))

    def test_rearm_needs_the_envelope_below_threshold_for_the_hold_time(self):
        # |x| = 0.2 for 20 ms, 0 for 60 ms, 0.2 for 20 ms. The follower (release 15 ms) is below theta - 3 dB
        # (0.0224) from 15 ln(0.2/0.0224) = 32.9 ms into the gap, i.e. for 27 ms: enough for a 20 ms hold, not 40 ms.
        x = np.concatenate([np.full(int(0.02 * FS), 0.2), np.zeros(int(0.06 * FS)), np.full(int(0.02 * FS), 0.2)])
        self.assertEqual(len(trigger.detect_onsets(x, FS, hold_ms=20.0, lockout_ms=5.0)), 2)
        self.assertEqual(len(trigger.detect_onsets(x, FS, hold_ms=40.0, lockout_ms=5.0)), 1)

    def test_silence_and_quiet_noise_give_no_events(self):
        quiet = np.random.default_rng(0).standard_normal(FS) * 10.0 ** (-50.0 / 20.0)  # peaks ~0.012 < 0.0316
        for x in (np.zeros(FS), quiet):
            self.assertEqual(len(trigger.detect_onsets(x, FS)), 0)


class TestVoice(unittest.TestCase):
    def test_voice_matches_the_spec_formulas(self):
        cases = [dict(), dict(f_start=120.0, f_end=30.0, tau_p_ms=10.0, attack_ms=5.0, t60_ms=1000.0, phase0_deg=90.0),
                 dict(f_start=40.0, f_end=60.0, phase0_deg=300.0, frac=0.37), dict(attack_ms=0.0, phase0_deg=45.0)]
        for fs in (44100, 48000):
            for kw in cases:
                frac = kw.get("frac", 0.0)
                got = trigger.voice(fs, int(0.5 * fs), t_on_frac=frac, **{k: v for k, v in kw.items() if k != "frac"})
                want = _formula(fs, int(0.5 * fs), **kw)
                for g, w, name in zip(got, want, ("y1", "y2")):
                    self.assertLess(np.max(np.abs(g - w)), 1e-12, msg=f"{name} fs {fs} {kw}")

    def test_pitch_and_amp_curves(self):
        # T-TRIG-03, measured rather than recomputed. Not via the analytic signal: for a decaying chirp the Hilbert
        # phase is 3-8 % off in frequency and the magnitude 0.3-0.7 dB off (the envelope is not narrow-band next to
        # a 45 Hz carrier). Instead the half period between successive zero crossings gives f at its midpoint, and
        # the local maxima of |y| give the envelope (a peak sample sits within 0.0x dB of a(t)).
        y1, _ = trigger.voice(FS, int(0.4 * FS))
        t = np.arange(len(y1)) / FS
        zc = np.sort(np.concatenate([util.zero_crossings(y1, True), util.zero_crossings(y1, False)]))
        t_c = (zc[1:] + zc[:-1]) / 2.0 / FS
        f_c = 0.5 * FS / np.diff(zc)
        sel = t_c >= 0.003
        f_formula = 45.0 + 35.0 * np.exp(-t_c / 0.030)
        self.assertGreater(np.sum(sel), 20)
        self.assertLessEqual(np.max(np.abs(f_c[sel] / f_formula[sel] - 1.0)), 0.005)
        peaks, _ = sps.find_peaks(np.abs(y1))
        peaks = peaks[(t[peaks] >= 0.003) & (t[peaks] <= 0.2)]
        a_formula = 10.0 ** (-3.0 * (t - 0.002) / 0.350)  # valid from the end of the attack
        self.assertGreater(len(peaks), 10)
        self.assertLessEqual(np.max(np.abs(20.0 * np.log10(np.abs(y1[peaks]) / a_formula[peaks]))), 0.1)

    def test_voice_is_silent_before_the_onset(self):
        for frac in (0.0, 0.5):
            y1, y2 = trigger.voice(FS, 100, t_on_frac=frac)
            self.assertEqual(y1[0], 0.0)
            self.assertEqual(y2[0], 0.0)
            self.assertTrue(np.all(np.isfinite(y1)) and np.all(np.isfinite(y2)))
        y1, _ = trigger.voice(FS, 100, t_on_frac=-0.5)  # live: the first output sample is already half a sample in
        self.assertNotEqual(y1[0], 0.0)

    def test_sub2_is_half_the_frequency(self):
        y1, y2 = trigger.voice(FS, FS)  # 1 s; past 0.3 s the pitch is f_end within 0.002 %
        seg = slice(int(0.3 * FS), None)
        self.assertLess(abs(util.zc_freq(y1[seg], FS) - 45.0) / 45.0, 0.005)
        self.assertLess(abs(util.zc_freq(y2[seg], FS) - 22.5) / 22.5, 0.005)


class TestRender(unittest.TestCase):
    def test_voice_determinism(self):
        # T-TRIG-02: identical hits on an integer-sample grid give identical voices. The first segment lacks the
        # 3 ms tail of a predecessor (-86 dB at 0.5 s), so hits 1..99 are compared with each other.
        x = signals.kick(FS, n_hits=100, spacing=0.5, level_db=-6.0)
        y = trigger.render(x, FS)
        hit = int(0.5 * FS)
        seg = lambda i: y[i * hit:(i + 1) * hit]
        self.assertLessEqual(max(util.null_db(seg(1), seg(i)) for i in range(2, 100)), -120.0)
        self.assertLessEqual(util.null_db(seg(1), seg(0)), -85.0)

    def test_render_equals_the_voice_for_a_single_hit(self):
        x = signals.kick(FS, n_hits=1, spacing=0.5, level_db=-6.0)
        t_i = float(trigger.detect_onsets(x, FS)[0])
        n_i, frac = int(np.floor(t_i)), t_i - np.floor(t_i)
        want = np.zeros(len(x))
        want[n_i:] = trigger.voice(FS, len(x) - n_i, t_on_frac=frac)[1]
        got = trigger.render(x, FS, velocity_pct=0.0, sub1_level=0.0, sub2_level=1.0)  # vel 0 -> V = 1
        self.assertLess(np.max(np.abs(got - want)), 1e-12)

    def test_single_hit_is_the_same_in_both_retrigger_modes(self):
        x = signals.kick(FS, n_hits=1, spacing=0.5, level_db=-6.0)
        np.testing.assert_allclose(trigger.render(x, FS, retrigger="legato"), trigger.render(x, FS, retrigger="restart"),
                                   atol=1e-12)

    def test_sub_levels_mix_the_two_voices(self):
        x = signals.kick(FS, n_hits=1, spacing=0.5, level_db=-6.0)
        y1 = trigger.render(x, FS)
        y2 = trigger.render(x, FS, sub1_level=0.0, sub2_level=1.0)
        np.testing.assert_allclose(trigger.render(x, FS, sub1_level=0.5, sub2_level=0.25), 0.5 * y1 + 0.25 * y2, atol=1e-12)

    def test_retrigger_restart_no_click(self):
        # Two kicks 100 ms apart; the first is still at -26 dBFS, so the threshold is raised to -20 dBFS and the
        # re-arm hold dropped. The old voice (0.145 of full scale at 0.1 s) is faded out over 3 ms, not cut.
        x = signals.kick(FS, n_hits=2, spacing=0.1, level_db=-6.0)
        on = trigger.detect_onsets(x, FS, thresh_db=-20.0, hold_ms=0.0)
        self.assertEqual(len(on), 2)
        y = trigger.render(x, FS, retrigger="restart", thresh_db=-20.0, hold_ms=0.0)
        self.assertLessEqual(_click(y, on[1]), 1e-3)  # T-TRIG-04
        step = np.sin(2.0 * np.pi * 80.0 * np.arange(4800) / FS)
        step[2400:] += 0.01
        self.assertGreater(_click(step, 2400), 9e-3)  # the metric does see a 0.01 step

    def test_retrigger_legato_phase_continuous(self):
        # T-TRIG-04, measured: no jump in the unwrapped analytic phase at the retrigger. Only the phase is required
        # to be continuous in Legato: the pitch envelope restarts (a rate step) and the attack restarts from the
        # current level, so the waveform has a slope kink there (up to ~0.009 of full scale, phase dependent);
        # this scenario's retrigger happens to fall near a zero crossing. The Hilbert phase itself has a floor of up
        # to ~0.08 rad near an amplitude kink at other phases, so the exact test below is the general guard.
        x = signals.kick(FS, n_hits=2, spacing=0.1, level_db=-6.0)
        on = trigger.detect_onsets(x, FS, thresh_db=-20.0, hold_ms=0.0)
        y = trigger.render(x, FS, retrigger="legato", thresh_db=-20.0, hold_ms=0.0)
        phase = np.unwrap(np.angle(sps.hilbert(y)))
        h = int(0.001 * FS)
        seg = phase[int(on[1]) - h:int(on[1]) + h + 1]
        self.assertLess(np.max(np.abs(np.diff(seg, 2))), 0.05)  # the phase has no jump (only a rate step)
        n = np.arange(4800)
        jumped = np.unwrap(np.angle(sps.hilbert(np.sin(2.0 * np.pi * 50.0 * n / FS + np.where(n >= 2400, 1.0, 0.0)))))
        self.assertGreater(np.max(np.abs(np.diff(jumped[2390:2411], 2))), 0.05)  # the metric does see a 1 rad jump

    def test_legato_retrigger_carries_phase_and_amplitude(self):
        # The running voice's phase and amplitude at the second onset are the start state of the new interval:
        # y(s) = A(s) sin(phi_c + inc(s)); A rises linearly from the current amplitude to 1 over the attack, then decays.
        inc = lambda s: 2.0 * np.pi * (45.0 * s + 35.0 * 0.030 * (1.0 - np.exp(-s / 0.030)))
        ta, t60 = 0.002, 0.350
        kw = dict(thresh_db=-12.0, hold_ms=0.0, lockout_ms=5.0)
        for spacing in (0.04, 0.06, 0.10, 0.15, 0.20):
            x = _bursts(2, spacing, amp=0.9)
            t0, t1 = trigger.detect_onsets(x, FS, **kw)
            y = trigger.render(x, FS, retrigger="legato", velocity_pct=0.0, **kw)  # V = 1
            s_old = (t1 - t0) / FS
            phi_c, a_c = inc(s_old), 10.0 ** (-3.0 * (s_old - ta) / t60)
            k = np.arange(int(np.ceil(t1)), len(x))
            s = (k - t1) / FS
            a = np.where(s < ta, a_c + (1.0 - a_c) * s / ta, 10.0 ** (-3.0 * (s - ta) / t60))
            self.assertLess(np.max(np.abs(y[k] - a * np.sin(phi_c + inc(s)))), 1e-9, msg=f"spacing {spacing}")

    def test_velocity_scaling(self):
        x = signals.kick(FS, n_hits=3, spacing=0.3, levels_db=[-26.0, -20.0, -14.0])  # peaks 0.05, 0.1, 0.2
        hit = int(0.3 * FS)
        peaks = lambda y: [np.max(np.abs(y[i * hit:i * hit + hit // 2])) for i in range(3)]
        p100 = peaks(trigger.render(x, FS, velocity_pct=100.0, thresh_db=-40.0))  # V = peak / 0.5
        self.assertAlmostEqual(20.0 * np.log10(p100[1] / p100[0]), 6.02, delta=0.5)
        self.assertAlmostEqual(20.0 * np.log10(p100[2] / p100[1]), 6.02, delta=0.5)
        p0 = peaks(trigger.render(x, FS, velocity_pct=0.0, thresh_db=-40.0))  # V = 1
        self.assertLess(20.0 * np.log10(max(p0) / min(p0)), 0.5)

    def test_velocity_is_clamped_to_1_5(self):
        x = signals.kick(FS, n_hits=1, spacing=0.5, level_db=-0.9)  # peak 0.9 -> V = 1.8 -> 1.5
        y = trigger.render(x, FS, velocity_pct=100.0)
        self.assertLessEqual(np.max(np.abs(y)), 1.5 + 1e-9)
        self.assertGreaterEqual(np.max(np.abs(y)), 1.45)

    def test_follow_ends_the_sub_with_the_source(self):
        x = np.concatenate([_bursts(1, 0.05, amp=0.2, length_ms=50.0), np.zeros(int(0.45 * FS))])  # 50 ms of 100 Hz
        free, follow = trigger.render(x, FS), trigger.render(x, FS, follow=True)
        seg = slice(int(0.15 * FS), int(0.20 * FS))  # 100-150 ms after the source ended
        self.assertGreaterEqual(util.db(util.rms(free[seg])) - util.db(util.rms(follow[seg])), 20.0)
        head = slice(0, int(0.02 * FS))  # the attack is not slowed down
        np.testing.assert_allclose(follow[head], free[head], atol=1e-12)

    def test_render_is_finite_for_close_hits_silence_dc_and_noise(self):
        # Hits 30 ms apart with a 60 ms lockout (detection of the ignored ones is covered above), silence, DC, noise.
        for x in (_bursts(8, 0.030), np.zeros(FS), np.full(FS, 0.3), np.random.default_rng(1).standard_normal(FS) * 1e-3):
            y = trigger.render(x, FS, hold_ms=0.0, lockout_ms=60.0)
            self.assertEqual(len(y), len(x))
            self.assertTrue(np.all(np.isfinite(y)))



class TestD8Fixes(unittest.TestCase):
    def test_follow_gain_is_causal(self):
        # D8 M4: the Follow normalisation divided by the maximum of the whole hit, known only offline; it now uses the
        # running maximum since the onset, so a prefix of the input gives the same prefix of the output.
        x = signals.kick(FS, n_hits=1, level_db=-12.0)
        x[int(0.030 * FS):] += signals.kick(FS, n_hits=1, level_db=-6.0)[:len(x) - int(0.030 * FS)]  # louder, in the lockout
        k = int(0.025 * FS)
        full = trigger.render(x, FS, follow=True)
        part = trigger.render(x[:k], FS, follow=True)
        np.testing.assert_allclose(full[:k], part, atol=1e-12)

    def test_zero_attack_at_90_degrees_does_not_click(self):
        # D8 M5: attack 0 with a start phase of 90 degrees jumped by the full amplitude; the attack is at least 0.5 ms
        y1, _ = trigger.voice(FS, 4800, attack_ms=0.0, phase0_deg=90.0)
        self.assertLessEqual(np.max(np.abs(np.diff(np.concatenate([[0.0], y1])))), 0.06)


if __name__ == "__main__":
    unittest.main()
