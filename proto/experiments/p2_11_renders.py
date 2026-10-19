# SPDX-License-Identifier: AGPL-3.0-or-later
"""P2-11: listening renders for the SPEC use cases UC1-UC5 into renders/ (git-ignored), with renders/README.txt.

A files are the dry, B files dry + sub, matched to the dry's A-weighted loudness (so B is not louder: listen for weight,
depth and tightness), W files the sub alone. The dry is delayed by the source band's group delay at the material's
fundamental (what align_ms does in the plug-in), so the sub is not late.
"""
import numpy as np

from experiments._common import REPO_ROOT, Recorder
from subgen_proto import band, drive, dynamics, harmonic, render, signals, svf, trigger, util

FS = 48000
OUT = REPO_ROOT / "renders"


def midi_hz(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def xb(x, lo=30.0, hi=120.0):
    return band.range_filter(x, FS, lo, hi)


def align_samples(f):
    """Group delay of the source band (BW4 30-120 Hz) at the material's fundamental."""
    secs = svf.design_butter(4, 30.0, FS, "high") + svf.design_butter(4, 120.0, FS, "low")
    return int(round(float(svf.group_delay_s(secs, [f], FS)[0]) * FS))


def delay(x, d):
    return np.concatenate([np.zeros(d), x[:len(x) - d]]) if d > 0 else x


def lowpass(x, fc=120.0):
    return svf.process_fast(svf.design_butter(4, fc, FS, "low"), x)


def wet_file(name, sub):
    pk = float(np.max(np.abs(sub)))
    return render.write_wav(OUT / f"{name}.wav", sub * (10 ** (-6.0 / 20.0) / pk if pk > 0 else 1.0), FS)


def main():
    rec = Recorder("p2_11")
    OUT.mkdir(exist_ok=True)
    written, notes = [], {}

    # ---------------------------------------------------------------- UC1: kick
    kick = signals.kick(FS, n_hits=8, spacing=0.5, level_db=-6.0)
    d = align_samples(60.0)
    subs = {"divide": lowpass(render.divide_sub(xb(kick), FS)), "trigger": 10 ** (-6.0 / 20.0) * trigger.render(xb(kick), FS)}
    dry = delay(kick, d)
    written += render.write_ab(OUT, "uc1_kick", dry, {k: dry + v for k, v in subs.items()}, FS)
    written += [wet_file(f"uc1_kick_W_{k}", v) for k, v in subs.items()]
    notes["uc1"] = f"align {1000 * d / FS:.1f} ms"

    # ---------------------------------------------------------------- UC2: 808 line and bass line
    s808 = np.concatenate([signals.bass_808(FS, f_end=midi_hz(m), glide_from=2.0 * midi_hz(m), level_db=-6.0) for m in (28, 31, 33, 36, 38)])
    bass = np.concatenate([signals.bass_pluck(FS, midi_hz(m), level_db=-6.0, seed=m) for m in (28, 33, 38, 43)])
    for name, x, f in (("uc2_808", s808, 45.0), ("uc2_bass", bass, 60.0)):
        d = align_samples(f)
        subs = {"divide": lowpass(render.divide_sub(xb(x), FS)), "track": lowpass(render.track_sub(xb(x), FS, lookahead_s=0.045))}
        dry = delay(x, d)
        written += render.write_ab(OUT, name, dry, {k: dry + v for k, v in subs.items()}, FS)
        written += [wet_file(f"{name}_W_{k}", v) for k, v in subs.items()]
        notes[name] = f"align {1000 * d / FS:.1f} ms"

    # ---------------------------------------------------------------- UC3: a bus (kick + bass + pad)
    n = 8 * FS // 2
    bus = signals.kick(FS, n_hits=8, spacing=0.5, level_db=-8.0)[:n].copy()
    line = np.concatenate([signals.bass_pluck(FS, midi_hz(m), dur=0.5, level_db=-10.0, seed=m) for m in (33, 33, 36, 31) * 2])
    bus[:len(line)] += np.roll(line, FS // 4)[:n]
    bus += signals.pink_noise(FS, n / FS, 200.0, 4000.0, level_db=-28.0, seed=9)
    d = align_samples(60.0)
    x_b = xb(bus)
    div = lowpass(render.divide_sub(x_b, FS))
    div_c, gr = dynamics.compressor(div, FS, thresh_db=-24.0, ratio=3.0, knee_db=6.0, attack_ms=10.0, release_ms=150.0, f_sub1=30.0)
    hyb = div_c + lowpass(harmonic.harmonic(x_b, FS, amount_pct=50.0), 600.0)
    dry = delay(bus, d)
    written += render.write_ab(OUT, "uc3_bus", dry, {"divide_comp": dry + div_c, "hybrid": dry + hyb}, FS)
    notes["uc3"] = f"align {1000 * d / FS:.1f} ms, sub compressor GR median {np.median(gr[gr < -0.1]) if np.any(gr < -0.1) else 0.0:.1f} dB"
    rec.check("UC3: with the sub off the output is the input (bit-exact)", float(not np.array_equal(dry + 0.0 * div_c, dry)), "==", 0.0)

    # ---------------------------------------------------------------- UC4: impact (TRIGGER + Drive)
    hit = np.zeros(2 * FS)
    burst = np.random.default_rng(11).standard_normal(int(0.03 * FS)) * np.exp(-np.arange(int(0.03 * FS)) / (0.008 * FS))
    hit[:len(burst)] = 0.4 * svf.process_fast(svf.design_butter(2, 100.0, FS, "high") + svf.design_butter(2, 8000.0, FS, "low"), burst)
    hit += signals.kick(FS, n_hits=1, spacing=2.0, level_db=-10.0, f_start=120.0, f_end=40.0, t60=0.6)
    impact = np.tile(hit, 3)
    voice = dict(f_start=120.0, f_end=35.0, tau_p_ms=60.0, attack_ms=2.0, t60_ms=1500.0)
    boom = trigger.render(xb(impact), FS, **voice)
    boom_drive = drive.dc_block(drive.process(0.5 * boom, FS, drive_pct=40.0, mode="soft", tone_hz=None), FS)
    d = align_samples(60.0)
    dry = delay(impact, d)
    written += render.write_ab(OUT, "uc4_impact", dry, {"trigger_drive": dry + boom_drive}, FS)
    written.append(wet_file("uc4_impact_W_trigger_drive", boom_drive))
    seg = [boom[i * 2 * FS + FS // 10:(i + 1) * 2 * FS - FS // 10] for i in range(3)]
    det = max(util.null_db(seg[1], seg[2]), util.null_db(seg[0], seg[1]))
    rec.info("UC4: voice-to-voice difference between the three identical impacts [dB]", round(det, 1))
    rec.check("UC4: identical hits give identical booms", det, "<=", -100.0, "dB")

    # ---------------------------------------------------------------- UC5: LFE in 5.1
    kick5 = signals.kick(FS, n_hits=4, spacing=0.75, level_db=-6.0)
    d = align_samples(60.0)
    lfe_feed = 10 ** (-10.0 / 20.0) * trigger.render(xb(kick5), FS)  # recorded 10 dB down: played back with +10 dB in-band gain
    six = render.lfe_51(delay(kick5, d), lfe_feed, FS)
    six *= min(1.0, 10 ** (-0.2 / 20.0) / float(np.max(np.abs(six))))
    written.append(render.write_wav(OUT / "uc5_lfe_51.wav", six, FS))
    sep = util.band_db(six[:, 3], FS, 0.0, 120.0) - util.band_db(six[:, 3], FS, 240.0, FS / 2)
    rec.info("UC5: LFE channel, energy up to 120 Hz over energy above 240 Hz [dB]", round(sep, 1))
    rec.check("UC5: LFE channel band-limited (>= 40 dB between <= 120 Hz and > 240 Hz)", sep, ">=", 40.0, "dB")
    rec.check("UC5: only FL, FR and LFE carry signal", float(np.max(np.abs(six[:, [2, 4, 5]]))), "==", 0.0)

    # ---------------------------------------------------------------- README
    size_mb = sum(p.stat().st_size for p in written) / 1e6
    readme = f"""SUB-GEN Phase 2 - listening renders (generated by proto/experiments/p2_11_renders.py)
================================================================================

All files: WAV, 24-bit, {FS} Hz. Synthetic test material (SPEC signals SIG-03/04/05 and composites), not music.
A = dry. B = dry + sub, matched to the A file's A-weighted loudness: B is NOT louder, so listen for weight, depth,
tightness and pitch, not for level. W = the sub alone (normalised to -6 dBFS peak).
The dry is delayed by the source band filter's group delay at the material's fundamental (what align_ms does in the
plug-in), so the sub is not late. Listen on full-range monitors or headphones that reach 30 Hz; a phone speaker will
only reveal the HARMONIC variants.

UC1 kick, 8 hits ({notes['uc1']})
  uc1_kick_A_dry / _B_divide / _B_trigger, uc1_kick_W_divide / _W_trigger
  Listen for: added weight around 35-50 Hz without smearing the click; every hit identical (polarity, no clicks).
  DIVIDE = half the kick's own pitch drop (75 -> 22 Hz); TRIGGER = a synthetic 80 -> 45 Hz boom (6 dB under its default).

UC2 808 line E1 G1 A1 C2 D2 ({notes['uc2_808']}) and plucked bass E1 A1 D2 G2 ({notes['uc2_bass']})
  uc2_808_* and uc2_bass_*: A_dry, B_divide, B_track, W_divide, W_track
  Listen for: the sub following the 808 glides, a clean sine, no octave jumps. DIVIDE follows instantly (half the
  frequency, waveform-locked); TRACK is a pure oscillator at f0/2 read 45 ms ahead (Mix) - smooth, but it settles
  only after the window catches up with fast glides.

UC3 bus: kick + bass + noise pad ({notes['uc3']})
  uc3_bus_A_dry, _B_divide_comp (DIVIDE + sub compressor), _B_hybrid (+ HARMONIC 50 %, audible on small speakers)
  Listen for: more depth without mud. With the sub off the bus is bit-identical to the input (checked).

UC4 impact: noise burst + thump, three identical hits
  uc4_impact_A_dry, _B_trigger_drive, uc4_impact_W_trigger_drive
  TRIGGER 120 -> 35 Hz (60 ms), T60 1.5 s, Soft drive 40 %. Listen for: the same boom three times (deterministic).

UC5 film LFE, 5.1 (FL FR FC LFE BL BR)
  uc5_lfe_51.wav: the kick in FL/FR, a TRIGGER sub in the LFE channel only, low-passed (24 dB/oct at 120 Hz) and
  recorded 10 dB down: play it through a 5.1 monitor path with the usual +10 dB LFE in-band gain.

Process your own material:
  PYTHONPATH=proto .venv/bin/python -m experiments.render_file your.wav --engine divide|track|trigger|harmonic
  (see --help: --mode add|wet, --sub-db, --drive, --range-lo/--range-hi, --lookahead-ms, --lp, --ceiling)

{len(written)} audio files, {size_mb:.1f} MB.
"""
    (OUT / "README.txt").write_text(readme)
    rec.info("files written", sorted(p.name for p in written))
    rec.info("total size [MB]", round(size_mb, 1))
    rec.check("renders stay under 500 MB", size_mb, "<=", 500.0, "MB")
    print("saved", rec.save())
    return rec


if __name__ == "__main__":
    main()
