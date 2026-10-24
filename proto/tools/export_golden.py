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
from scipy import signal as sps

from subgen_proto import activity, divide, envelope, render, signals, svf, track, util

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


FILTER_FS = (44100, 48000, 96000, 192000)
FILTER_FC = (15.0, 22.0, 30.0, 60.0, 120.0, 500.0)
FILTER_DESIGNS = (("BW", 2), ("BW", 4), ("BW", 8), ("BW", 16), ("LR", 4), ("LR", 8), ("LR", 16),
                  ("BESSEL", 2), ("BESSEL", 4), ("BESSEL", 8))


def _scipy_design(family: str, order: int, fc: float, fs: float, kind: str, output: str):
    """SciPy's digital design (bilinear, prewarped at fc) and the power it is raised to (LR = Butterworth squared)."""
    if family == "BW":
        return sps.butter(order, fc, btype=kind, fs=fs, output=output), 1
    if family == "LR":
        return sps.butter(order // 2, fc, btype=kind, fs=fs, output=output), 2
    return sps.bessel(order, fc, btype=kind, fs=fs, output=output, norm="mag"), 1


def _group_delay_s(zpk, freqs, fs: float) -> np.ndarray:
    """Exact group delay from the poles and zeros: tau = sum_p Re 1/(1 - p e^-jw) - sum_z Re 1/(1 - z e^-jw) samples
    (scipy.signal.group_delay's polynomial form is singular for the zeros at z = 1 of the high-pass sections)."""
    zeros, poles, _ = zpk
    e = np.exp(-2j * np.pi * np.asarray(freqs, dtype=np.float64) / fs)
    tau = sum(np.real(1.0 / (1.0 - p * e)) for p in poles) - sum(np.real(1.0 / (1.0 - z * e)) for z in zeros)
    return tau / fs


def export_filters() -> dict[str, np.ndarray]:
    """Task 5: |H| (dB) and group delay (s) of SciPy's Butterworth, Linkwitz-Riley and Bessel (norm 'mag') LP/HP on 64
    log-spaced frequencies from 5 Hz to 0.45 fs (freqs/<fs>); per design and fs one array of 6 x 64 values, fc-major
    in the order of FILTER_FC, plus the group delay at each fc (T-FILT-01, T-FILT-04)."""
    out = {}
    for fs in FILTER_FS:
        freqs = np.geomspace(5.0, 0.45 * fs, 64)
        out[f"freqs/{fs}"] = freqs
        for family, order in FILTER_DESIGNS:
            for kind, tag in (("low", "LP"), ("high", "HP")):
                mags, delays, at_fc = [], [], []
                for fc in FILTER_FC:
                    sos, power = _scipy_design(family, order, fc, fs, kind, "sos")
                    zpk, _ = _scipy_design(family, order, fc, fs, kind, "zpk")
                    _, h = sps.sosfreqz(sos, worN=freqs, fs=fs)
                    mags.append(power * 20.0 * np.log10(np.abs(h)))
                    delays.append(power * _group_delay_s(zpk, freqs, fs))
                    at_fc.append(power * _group_delay_s(zpk, [fc], fs)[0])
                key = f"{family}{order}/{tag}/{fs}"
                out[f"mag_db/{key}"] = np.concatenate(mags)
                out[f"gd_s/{key}"] = np.concatenate(delays)
                out[f"gd_fc_s/{key}"] = np.array(at_fc)
    return out


def export_envelope() -> dict[str, np.ndarray]:
    """Task 6: followers and the causal sliding maximum on the first 4096 samples of a kick and of xorshift noise
    (both regenerated by the C++ test), 48 kHz."""
    fs = 48000
    out = {}
    for name, x in (("kick", signals.kick(fs)[:EXCERPT]), ("noise", signals.xorshift_noise(EXCERPT, seed=13))):
        out[f"peak_1_50/{name}"] = envelope.peak_follower(x, fs, 1.0, 50.0)
        out[f"peak_0.2_15/{name}"] = envelope.peak_follower(x, fs, 0.2, 15.0)
        out[f"rms_20/{name}"] = envelope.rms_follower(x, fs, 20.0)
        out[f"sliding_peak_480/{name}"] = envelope.sliding_peak(np.abs(x), 480)
    return out


def source_band(x, fs: float, lo: float = 30.0, hi: float = 120.0, order: int = 4) -> np.ndarray:
    """x_b with the exact TPT loop of the C++ SourceBand (HP sections, then LP sections); band.range_filter runs the
    same designs through scipy's sosfilt, which rounds differently."""
    return svf.process(svf.design_butter(order, lo, fs, "high") + svf.design_butter(order, hi, fs, "low"), x)


def notes_inputs(fs: float = 48000) -> dict[str, np.ndarray]:
    """Task 7: five SIG-03 hits through the source band: in silence, over xorshift band noise at -60 dBFS RMS, and
    every 0.45 s over T60 1 s tails. tests/unit/test_engine_common.cpp builds the same inputs."""
    hits = signals.kick(fs, n_hits=5, spacing=0.5, level_db=-6.0)
    noise = signals.band_noise(fs, 2.5, 30.0, 120.0, level_db=0.0, seed=5)
    noise = noise * (10.0 ** (-60.0 / 20.0) / np.sqrt(np.mean(noise * noise)))
    tails = signals.kick(fs, n_hits=5, spacing=0.45, level_db=-6.0, t60=1.0)
    return {"silence": source_band(hits, fs), "noise_60": source_band(hits + noise, fs), "tails": source_band(tails, fs)}


def export_notes() -> dict[str, np.ndarray]:
    """Task 7: note starts (detect, begin) and ends of activity.note_starts / note_ends on notes_inputs()."""
    out = {}
    for name, x_b in notes_inputs().items():
        starts = activity.note_starts(x_b, 48000)
        out[f"detect/{name}"] = starts.detect
        out[f"begin/{name}"] = starts.begin
        out[f"end/{name}"] = activity.note_ends(x_b, 48000).astype(np.float64)
    return out


DIVIDE_WINDOW = 4096


def noisy_sine(f: float, fs: float, dur: float, snr_db: float, seed: int) -> np.ndarray:
    """A -12 dBFS sine plus band noise (30-120 Hz) snr_db below it (tests/unit/test_divide.cpp: noiseSine)."""
    s = signals.sine(f, fs, dur, -12.0)
    nz = signals.band_noise(fs, dur, 30.0, 120.0, 0.0, seed)
    return s + nz * ((util.rms(s) / 10 ** (snr_db / 20.0)) / util.rms(nz))


def divide_cases(fs: float = 48000) -> dict[str, tuple[np.ndarray, str, int]]:
    """Task 8: raw input (the C++ test runs it through its own SourceBand), quality and the first sample of the
    compared window. tests/unit/test_divide.cpp builds the same inputs."""
    sine = np.concatenate([np.zeros(int(0.02 * fs)), signals.sine(55.0, fs, 0.1, -12.0)])
    noisy = noisy_sine(55.0, fs, 1.5, 15.0, 5)
    return {
        "sine_live": (sine, "live", 0),
        "kick_mix": (signals.kick(fs, n_hits=1, spacing=0.5, level_db=-6.0), "mix", 0),
        "two_hits_mix": (signals.kick(fs, n_hits=2, spacing=0.5, level_db=-6.0), "mix", 24000 - 1024),
        "tails_mix": (signals.kick(fs, n_hits=2, spacing=0.45, level_db=-6.0, t60=1.0), "mix", 21600 - 1024),
        "bass808_live": (signals.bass_808(fs), "live", 0),
        # F-1: each window holds a gate opening on the second confirmation and samples that one would have opened
        "noisy_sine_mix": (noisy, "mix", 46336),
        "noisy_sine_live": (noisy, "live", 48384),
    }


def export_divide() -> dict[str, np.ndarray]:
    """Task 8: divide.divide (the delayed axis: y1, y2, f_est, conf, provisional) and render.divide_sub_causal (Sub -1,
    shape 0, conf_gate 50 %: the engine output) on x_b = source_band(input), in windows of DIVIDE_WINDOW samples."""
    fs = 48000
    out = {}
    for name, (x, quality, first) in divide_cases(fs).items():
        x_b = source_band(x, fs)
        look = render.LOOKAHEAD_S[quality]
        res = divide.divide(x_b, fs, live=quality == "live", lookahead_s=look)
        gated = render.divide_sub_causal(x_b, fs, quality=quality)
        w = slice(first, first + DIVIDE_WINDOW)
        out[f"y1/{name}"] = res.y1[w]
        out[f"y2/{name}"] = res.y2[w]
        out[f"f_est/{name}"] = res.f_est[w]
        out[f"conf/{name}"] = res.conf[w]
        out[f"provisional/{name}"] = res.provisional[w].astype(np.float64)
        out[f"out/{name}"] = gated[w]
    return out


def pitch_cases(fs: float = 48000) -> dict[str, np.ndarray]:
    """Task 9: raw input of an SIG-04 and an SIG-05 note (the C++ test runs it through its own SourceBand, 30-120 Hz).
    tests/unit/test_pitch.cpp builds the same inputs."""
    return {"bass808_a1": signals.bass_808(fs, f_end=55.0, glide_from=110.0, harm2_db=-8.0),
            "pluck_e1": signals.bass_pluck(fs, 41.2, noise="xorshift", seed=1)}


def export_pitch() -> dict[str, np.ndarray]:
    """Task 9: track.mpm (f_min 30 Hz = range_lo, prior off) on x_b = source_band(input): frame times in samples, raw f0
    (NaN without an estimate) and confidence, and track.apply_prior of them."""
    fs = 48000
    out = {}
    for name, x in pitch_cases(fs).items():
        trk = track.mpm(source_band(x, fs), fs, f_min=30.0, prior=False)
        out[f"t/{name}"] = np.round(trk.t * fs)
        out[f"f0_raw/{name}"] = trk.f0
        out[f"conf/{name}"] = trk.conf
        out[f"f0/{name}"] = track.apply_prior(trk.f0, trk.conf)
    return out


EXPORTS = {"signals": export_signals, "filters": export_filters, "envelope": export_envelope, "notes": export_notes,
           "divide": export_divide, "pitch": export_pitch}


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
