# SPDX-License-Identifier: AGPL-3.0-or-later
"""render_file: add a SUB-GEN prototype sub to your own WAV file.

    PYTHONPATH=proto .venv/bin/python -m experiments.render_file song.wav --engine divide
    PYTHONPATH=proto .venv/bin/python -m experiments.render_file kick.wav --engine trigger --sub-db -6 --drive 30
    PYTHONPATH=proto .venv/bin/python -m experiments.render_file bass.wav --engine track --mode wet --out bass_sub.wav

The input is summed to mono for detection (SPEC §5.2 Sum) and band-limited to [range_lo, range_hi] (§5.3); the engine
turns that into a sub, which then goes through level -> Drive (+ 5 Hz DC blocker) -> low-pass -> lookahead limiter
(5 ms, like Print). 'add' delays the dry by the limiter's lookahead and adds the sub to every channel (the result is
scaled down if it would clip); 'wet' writes the sub alone (mono). Output: 24-bit WAV at the input's sample rate.
This is an offline prototype for listening, not the plug-in: no surround routing, no LFE mode, no phase tools.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile as sf

from subgen_proto import band, drive, dynamics, render, svf

LIMITER_LOOKAHEAD_MS = 5.0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="render_file", description="Add a SUB-GEN prototype sub to a WAV file.")
    p.add_argument("input", help="input WAV (any channel count)")
    p.add_argument("--engine", choices=render.ENGINES, default="divide")
    p.add_argument("--out", help="output WAV (default: <input>_<engine>_<mode>.wav next to the input)")
    p.add_argument("--mode", choices=("add", "wet"), default="add", help="add the sub to the dry, or write the sub alone")
    p.add_argument("--range-lo", type=float, default=30.0, help="source band low edge [Hz] (SPEC range_lo)")
    p.add_argument("--range-hi", type=float, default=120.0, help="source band high edge [Hz] (SPEC range_hi)")
    p.add_argument("--sub-db", type=float, default=0.0, help="sub level [dB]")
    p.add_argument("--shape", type=float, default=0.0, help="DIVIDE/TRACK shape [%%]: 0 = sine, 100 = native")
    p.add_argument("--lookahead-ms", type=float, default=45.0, help="TRACK pitch lookahead [ms] (Mix 45, Print 55, Live 0)")
    p.add_argument("--harm-amount", type=float, default=50.0, help="HARMONIC amount [%%]")
    p.add_argument("--drive", type=float, default=0.0, help="Drive amount [%%] (0 = off)")
    p.add_argument("--drive-mode", choices=drive.MODES, default="soft")
    p.add_argument("--lp", type=float, default=None, help="sub low-pass [Hz] (default 120, or 600 for HARMONIC)")
    p.add_argument("--ceiling", type=float, default=-1.0, help="sub limiter ceiling [dBFS]")
    return p


def main(argv=None) -> Path:
    a = build_parser().parse_args(argv)
    x, fs = sf.read(a.input, always_2d=True)
    x_b = band.range_filter(np.mean(x, axis=1), fs, a.range_lo, a.range_hi)
    kw = {"divide": dict(shape_pct=a.shape),
          "track": dict(shape_pct=a.shape, lookahead_s=a.lookahead_ms / 1000.0, f_min=a.range_lo),
          "trigger": {},
          "harmonic": dict(amount_pct=a.harm_amount, f_lo=a.range_lo)}[a.engine]
    sub = render.engine_sub(x_b, fs, a.engine, **kw) * 10.0 ** (a.sub_db / 20.0)
    if a.drive > 0.0:
        sub = drive.dc_block(drive.process(sub, fs, drive_pct=a.drive, mode=a.drive_mode), fs)
    lp = a.lp if a.lp is not None else (600.0 if a.engine == "harmonic" else 120.0)
    sub = svf.process_fast(svf.design_butter(4, lp, fs, "low"), sub)
    sub = dynamics.limiter(sub, fs, ceiling_db=a.ceiling, lookahead_ms=LIMITER_LOOKAHEAD_MS, release_ms=100.0)
    if a.mode == "wet":
        y = sub
    else:
        d = int(round(LIMITER_LOOKAHEAD_MS * 1e-3 * fs))
        dry = np.vstack([np.zeros((d, x.shape[1])), x[:len(x) - d]])
        y = dry + sub[:, None]
        if x.shape[1] == 1:
            y = y[:, 0]
        peak = float(np.max(np.abs(y)))
        if peak > 1.0:
            y = y * (10.0 ** (-0.2 / 20.0) / peak)
            print(f"render_file: the sum peaked at {20 * np.log10(peak):+.1f} dBFS; scaled to -0.2 dBFS")
    out = Path(a.out) if a.out else Path(a.input).with_name(f"{Path(a.input).stem}_{a.engine}_{a.mode}.wav")
    render.write_wav(out, y, fs)
    print(f"render_file: {a.engine}, {a.mode}, band {a.range_lo:g}-{a.range_hi:g} Hz, sub {a.sub_db:+g} dB, "
          f"drive {a.drive:g} %, low-pass {lp:g} Hz, dry delayed {LIMITER_LOOKAHEAD_MS:g} ms -> {out}")
    return out


if __name__ == "__main__":
    main()
