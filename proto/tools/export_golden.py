# SPDX-License-Identifier: AGPL-3.0-or-later
"""Golden data for the C++ tests (tests/golden/*.sgg), computed by the reference prototype.

Each export_<module>() returns {name: float64 array}; main() writes <module>.sgg into --out, or with --check compares
the bytes with the files already there (exit 1 on any difference or missing file). The C++ side reads them with
tests/support/Golden.

.sgg format, little-endian: b"SGG1", uint32 count, then count times: uint16 name length, UTF-8 name, uint64 n,
float64[n].

Usage: PYTHONPATH=proto python proto/tools/export_golden.py --out tests/golden [--check]
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np

from subgen_proto import signals

MAGIC = b"SGG1"
EXCERPT = 4096


def encode_sgg(arrays: dict[str, np.ndarray]) -> bytes:
    parts = [MAGIC, struct.pack("<I", len(arrays))]
    for name, values in arrays.items():
        raw = name.encode("utf-8")
        data = np.ascontiguousarray(values, dtype="<f8")
        parts += [struct.pack("<H", len(raw)), raw, struct.pack("<Q", data.size), data.tobytes()]
    return b"".join(parts)


def write_sgg(path: Path, arrays: dict[str, np.ndarray]) -> None:
    Path(path).write_bytes(encode_sgg(arrays))


def read_sgg(path: Path) -> dict[str, np.ndarray]:
    data = Path(path).read_bytes()
    if data[:4] != MAGIC:
        raise ValueError(f"{path}: not an SGG1 file")
    (count,), pos, out = struct.unpack_from("<I", data, 4), 8, {}
    for _ in range(count):
        (length,) = struct.unpack_from("<H", data, pos)
        name = data[pos + 2:pos + 2 + length].decode("utf-8")
        (n,) = struct.unpack_from("<Q", data, pos + 2 + length)
        pos += 2 + length + 8
        out[name] = np.frombuffer(data, dtype="<f8", count=n, offset=pos).astype(np.float64)
        pos += 8 * n
    return out


def export_signals() -> dict[str, np.ndarray]:
    """Task 4: the first 4096 samples of the generators the C++ tests reimplement (tests/unit/test_support.cpp)."""
    return {
        "sine": signals.sine(55.0, 48000, 0.1, -6.0, 0.3)[:EXCERPT],
        "log_sweep": signals.log_sweep(20.0, 200.0, 48000, 0.1)[:EXCERPT],
        "kick": signals.kick(48000)[:EXCERPT],
        "bass_808": signals.bass_808(44100)[:EXCERPT],
        "two_tone": signals.two_tone(31.0, 81.0, 48000, 0.1)[:EXCERPT],
        "square": signals.square(40.0, 48000, 0.1, -6.0)[:EXCERPT],
        "xorshift_noise": signals.xorshift_noise(EXCERPT, seed=7),
        "band_noise": signals.band_noise(48000, 0.1, 30.0, 120.0, -12.0, seed=4)[:EXCERPT],
    }


EXPORTS = {"signals": export_signals}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Export or check the golden data of the C++ tests.")
    ap.add_argument("--out", type=Path, required=True, help="directory of the .sgg files (tests/golden)")
    ap.add_argument("--check", action="store_true", help="compare with the files in --out instead of writing")
    args = ap.parse_args(argv)
    stale = []
    if not args.check:
        args.out.mkdir(parents=True, exist_ok=True)
    for module, export in EXPORTS.items():
        path = args.out / f"{module}.sgg"
        data = encode_sgg(export())
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                stale.append(path.name)
        else:
            path.write_bytes(data)
    for name in stale:
        print(f"{name}: missing or different from the prototype's output", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
