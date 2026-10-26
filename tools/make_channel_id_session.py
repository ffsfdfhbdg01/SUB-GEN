#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The channel-identification session for the Cubase check (SPK-2, docs/checklists/SPK-2_cubase.md).

One interleaved WAV per layout (5.0, 5.1, 7.1.2, 7.1.4), WAVE_FORMAT_EXTENSIBLE with the channel mask so that a DAW
puts each channel on its speaker, 48 kHz, 24-bit: SIG-15 (TEST_PLAN) after a lead of silence. Channel k (in the
file's order) carries its own tone, 1 kHz + 100*k Hz at -20 dBFS, and the 60 Hz bed at its class's level (front
-20.0, centre -18.4, surround -21.9, height -24.4, LFE -26.0 dBFS), so tools/check_bounce.py can tell where each
channel and the sub went. Standard library only.

Usage: make_channel_id_session.py [--out renders/spk2] [--seconds 8] [--lead 0.5]
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from array import array
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FS = 48000
TONE_AMPLITUDE = 0.1  # -20 dBFS

# WAVE_FORMAT_EXTENSIBLE speaker positions (dwChannelMask bits) and the classes of SPEC_IO_UI §1
SPEAKERS = {
    "L": (0x1, "front"),
    "R": (0x2, "front"),
    "C": (0x4, "centre"),
    "LFE": (0x8, "lfe"),
    "Lrs": (0x10, "surround"),  # back left
    "Rrs": (0x20, "surround"),
    "Ls": (0x200, "surround"),  # side left
    "Rs": (0x400, "surround"),
    "Ltf": (0x1000, "height"),  # top front left
    "Rtf": (0x4000, "height"),
    "Ltr": (0x8000, "height"),  # top back left
    "Rtr": (0x20000, "height"),
}
BED = {"front": 0.10, "centre": 0.12, "surround": 0.08, "height": 0.06, "lfe": 0.05}  # the 60 Hz bed per class

# a WAV interleaves its channels in the order of their mask bits
LAYOUTS = {
    "5.0": ["L", "R", "C", "Ls", "Rs"],
    "5.1": ["L", "R", "C", "LFE", "Ls", "Rs"],
    "7.1.2": ["L", "R", "C", "LFE", "Lrs", "Rrs", "Ls", "Rs", "Ltf", "Rtf"],
    "7.1.4": ["L", "R", "C", "LFE", "Lrs", "Rrs", "Ls", "Rs", "Ltf", "Rtf", "Ltr", "Rtr"],
}

PCM = b"\x01\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71"  # KSDATAFORMAT_SUBTYPE_PCM
FLOAT = b"\x03\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71"  # KSDATAFORMAT_SUBTYPE_IEEE_FLOAT


def mask(layout: str) -> int:
    return sum(SPEAKERS[s][0] for s in LAYOUTS[layout])


def tone_hz(k: int) -> float:
    """The identification tone of the file's channel k."""
    return 1000.0 + 100.0 * k


def signals(layout: str, seconds: float, lead: float, fs: int = FS) -> list[list[float]]:
    """SIG-15 for the layout: `lead` seconds of silence, then each channel's tone and its class's 60 Hz bed."""
    start, n = round(lead * fs), round((lead + seconds) * fs)
    out = []
    for k, speaker in enumerate(LAYOUTS[layout]):
        bed = BED[SPEAKERS[speaker][1]]
        w1, w2 = 2.0 * math.pi * tone_hz(k) / fs, 2.0 * math.pi * 60.0 / fs
        out.append([0.0] * start + [TONE_AMPLITUDE * math.sin(w1 * i) + bed * math.sin(w2 * i) for i in range(n - start)])
    return out


def write_wav(path: Path, channels: list[list[float]], fs: int, channel_mask: int, sample_format: str = "pcm24",
              extensible: bool = True) -> None:
    """Interleaved WAV: sample_format pcm16, pcm24 or float32; WAVE_FORMAT_EXTENSIBLE (with the mask) or plain."""
    n = len(channels[0])
    interleaved = [channels[c][i] for i in range(n) for c in range(len(channels))]
    if sample_format == "float32":
        samples = array("f", interleaved)
        bits, subformat, tag = 32, FLOAT, 3
    else:
        bits = 16 if sample_format == "pcm16" else 24
        top = (1 << (bits - 1)) - 1
        samples = array("i", (max(-top - 1, min(top, round(v * top))) for v in interleaved))
        subformat, tag = PCM, 1
    if sys.byteorder == "big":
        samples.byteswap()
    data = samples.tobytes()
    if bits == 24:  # the low three bytes of each little-endian int32
        packed = bytearray(len(data) // 4 * 3)
        for b in range(3):
            packed[b::3] = data[b::4]
        data = bytes(packed)
    elif bits == 16:
        packed = bytearray(len(data) // 2)
        for b in range(2):
            packed[b::2] = data[b::4]
        data = bytes(packed)
    block = len(channels) * bits // 8
    if extensible:
        fmt = struct.pack("<HHIIHHHHI16s", 0xFFFE, len(channels), fs, fs * block, block, bits, 22, bits, channel_mask,
                          subformat)
    else:
        fmt = struct.pack("<HHIIHH", tag, len(channels), fs, fs * block, block, bits)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    if len(data) % 2:
        body += b"\x00"
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", type=Path, default=REPO / "renders" / "spk2")
    ap.add_argument("--seconds", type=float, default=8.0, help="the signal after the lead")
    ap.add_argument("--lead", type=float, default=0.5, help="silence first (the onset shows the latency compensation)")
    args = ap.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    for layout, speakers in LAYOUTS.items():
        path = args.out / f"spk2_{layout}.wav"
        write_wav(path, signals(layout, args.seconds, args.lead), FS, mask(layout))
        legend = ", ".join(f"{s} {tone_hz(k):.0f} Hz" for k, s in enumerate(speakers))
        print(f"{path}: {legend}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
