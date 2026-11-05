#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reads a bounce of the channel-identification session (tools/make_channel_id_session.py) and reports, per channel,
which identification tone it carries (so where each source channel went), the 60 Hz bed, the sub at 30 Hz (DIVIDE's
Sub -1 of the bed; above -50 dBFS counts as sub) and the onset (the session starts after its lead of silence, so with
the latency compensated the onset is the lead). Standard library only.

Usage: check_bounce.py BOUNCE.wav [--layout 5.1] [--lead 0.5]
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from array import array
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_channel_id_session as session  # noqa: E402

SUB_HZ, BED_HZ = 30.0, 60.0
SUB_FLOOR_DB = -50.0   # a sub at or above this counts
TONE_FLOOR_DB = -60.0  # no identification tone below this
ONSET_DB = -40.0


@dataclass
class Wav:
    fs: int
    channel_mask: int  # 0 without WAVE_FORMAT_EXTENSIBLE
    channels: list[list[float]]


@dataclass
class Channel:
    speaker: str        # from the bounce's channel mask ("?" without one)
    tone: int | None    # the source channel k whose tone this channel carries
    tone_db: float
    bed_db: float
    sub_db: float
    onset_ms: float | None


def read_wav(path: Path) -> Wav:
    """PCM 16/24/32-bit or IEEE float 32/64-bit, plain or WAVE_FORMAT_EXTENSIBLE; ValueError for anything else."""
    raw = path.read_bytes()
    if len(raw) < 12 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file" + (" (RF64: export without RF64)" if raw[:4] == b"RF64" else ""))
    fmt = data = None
    pos = 12
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None or len(fmt) < 16:
        raise ValueError(f"{path}: no fmt or data chunk")
    tag, nch, fs, _, block, bits = struct.unpack("<HHIIHH", fmt[:16])
    channel_mask = 0
    if tag == 0xFFFE and len(fmt) >= 40:
        channel_mask = struct.unpack("<I", fmt[20:24])[0]
        tag = struct.unpack("<H", fmt[24:26])[0]
    if nch < 1 or block != nch * bits // 8:
        raise ValueError(f"{path}: inconsistent format")
    data = data[: len(data) // block * block]
    if tag == 3 and bits in (32, 64):
        samples = array("f" if bits == 32 else "d")
        samples.frombytes(data)
        if sys.byteorder == "big":
            samples.byteswap()
        values = list(samples)
    elif tag == 1 and bits in (16, 24, 32):
        if bits == 24:  # sign-extend each three bytes to an int32
            wide = bytearray(len(data) // 3 * 4)
            for b in range(3):
                wide[b::4] = data[b::3]
            wide[3::4] = bytes(0xFF if v & 0x80 else 0 for v in data[2::3])
            data = bytes(wide)
        samples = array("h" if bits == 16 else "i")
        samples.frombytes(data)
        if sys.byteorder == "big":
            samples.byteswap()
        scale = float(1 << (bits - 1))
        values = [v / scale for v in samples]
    else:
        raise ValueError(f"{path}: unsupported format (tag {tag}, {bits}-bit)")
    return Wav(fs, channel_mask, [values[c::nch] for c in range(nch)])


def speakers(channel_mask: int, count: int) -> list[str]:
    """The speakers of a channel mask in the file's order (the mask's bit order); '?' past the mask."""
    by_bit = sorted((bit, name) for name, (bit, _) in session.SPEAKERS.items())
    named = [name for bit, name in by_bit if channel_mask & bit]
    return (named + ["?"] * count)[:count]


def level_db(x: list[float], fs: int, hz: float) -> float:
    """The amplitude of the sine at `hz` in dBFS (a full-scale sine reads 0): Hann-windowed Goertzel."""
    n = len(x)
    if n < 2:
        return -300.0
    w = 2.0 * math.pi * hz / fs
    coeff = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    step = 2.0 * math.pi / (n - 1)
    for i, v in enumerate(x):
        s0 = v * (0.5 - 0.5 * math.cos(step * i)) + coeff * s1 - s2
        s2, s1 = s1, s0
    power = max(s1 * s1 + s2 * s2 - coeff * s1 * s2, 0.0)
    amplitude = 2.0 * math.sqrt(power) / (0.5 * (n - 1))
    return 20.0 * math.log10(max(amplitude, 1e-15))


def analyse(wav: Wav, lead: float = 0.5, window: float = 1.0) -> list[Channel]:
    """Per channel: the strongest identification tone among the file's, the bed, the sub and the onset. The levels are
    read over `window` seconds from one second after the lead (past the onset and the engine's lock-in)."""
    start = round((lead + 1.0) * wav.fs)
    stop = round((lead + 1.0 + window) * wav.fs)
    threshold = 10.0 ** (ONSET_DB / 20.0)
    names = speakers(wav.channel_mask, len(wav.channels))
    out = []
    for name, x in zip(names, wav.channels):
        part = x[start:stop] if len(x) >= stop else x[len(x) // 2:]
        tones = [level_db(part, wav.fs, session.tone_hz(k)) for k in range(max(len(wav.channels), 12))]
        best = max(range(len(tones)), key=lambda k: tones[k])
        onset = next((i for i, v in enumerate(x) if abs(v) > threshold), None)
        out.append(Channel(name, best if tones[best] > TONE_FLOOR_DB else None, tones[best],
                           level_db(part, wav.fs, BED_HZ), level_db(part, wav.fs, SUB_HZ),
                           None if onset is None else 1000.0 * onset / wav.fs))
    return out


def report(wav: Wav, channels: list[Channel], layout: str | None) -> str:
    source = session.LAYOUTS.get(layout or "", [])
    lines = [f"{len(wav.channels)} channels, {wav.fs} Hz, mask 0x{wav.channel_mask:X}",
             " ch  speaker  tone of            tone dB   bed dB    sub dB   onset ms"]
    moved = []
    for j, c in enumerate(channels):
        if c.tone is None:
            who = "-"
        else:
            who = f"{session.tone_hz(c.tone):.0f} Hz" + (f" ({source[c.tone]})" if c.tone < len(source) else "")
            if source and c.tone != j:
                moved.append(f"channel {j} carries source channel {c.tone}")
        onset = "-" if c.onset_ms is None else f"{c.onset_ms:.1f}"
        lines.append(f"{j:3d}  {c.speaker:7s}  {who:17s} {c.tone_db:7.1f}  {c.bed_db:7.1f}  {c.sub_db:7.1f}  {onset:>8s}")
    subs = [f"{j} ({c.speaker})" for j, c in enumerate(channels) if c.sub_db >= SUB_FLOOR_DB]
    lines.append("sub (30 Hz >= -50 dBFS) in: " + (", ".join(subs) if subs else "none"))
    if source:
        lines.append("channel order: " + ("as the session's" if not moved else "; ".join(moved)))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("bounce", type=Path)
    ap.add_argument("--layout", choices=sorted(session.LAYOUTS), help="the session file the bounce came from")
    ap.add_argument("--lead", type=float, default=0.5)
    args = ap.parse_args(argv)
    try:
        wav = read_wav(args.bounce)
    except (OSError, ValueError) as e:
        print(e, file=sys.stderr)
        return 1
    print(f"{args.bounce}: " + report(wav, analyse(wav, args.lead), args.layout))
    return 0


if __name__ == "__main__":
    sys.exit(main())
