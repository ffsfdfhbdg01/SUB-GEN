# SPDX-License-Identifier: AGPL-3.0-or-later
"""Deterministic test signals SIG-01..SIG-19 (float64, mono). `level_db` is the peak level in dBFS."""
from __future__ import annotations

import numpy as np
from scipy import signal as sps


def _amp(level_db: float) -> float:
    return 10.0 ** (level_db / 20.0)


def _n(fs: float, dur: float) -> int:
    return int(round(fs * dur))


def _normalize_peak(x: np.ndarray, level_db: float) -> np.ndarray:
    pk = np.max(np.abs(x))
    return x if pk == 0.0 else x * (_amp(level_db) / pk)


def sine(f: float, fs: float, dur: float, level_db: float = -6.0, phase: float = 0.0) -> np.ndarray:
    t = np.arange(_n(fs, dur)) / fs
    return _amp(level_db) * np.sin(2.0 * np.pi * f * t + phase)


def log_sweep(f0: float, f1: float, fs: float, dur: float, level_db: float = -12.0) -> np.ndarray:
    t = np.arange(_n(fs, dur)) / fs
    r = f1 / f0
    phase = 2.0 * np.pi * f0 * dur / np.log(r) * (r ** (t / dur) - 1.0)
    return _amp(level_db) * np.sin(phase)


def kick(fs, n_hits=1, spacing=0.5, level_db=-6.0, levels_db=None, f_start=150.0, f_end=45.0,
         tau_pitch=0.030, t60=0.300, phase=0.0) -> np.ndarray:
    """SIG-03: sine with exponential pitch drop f_start -> f_end (time constant tau_pitch) and
    exponential decay reaching -60 dB after t60. Each hit is peak-normalised to its own level."""
    hit_n = _n(fs, spacing)
    t = np.arange(hit_n) / fs
    phi = 2.0 * np.pi * (f_end * t + (f_start - f_end) * tau_pitch * (1.0 - np.exp(-t / tau_pitch))) + phase
    one = 10.0 ** (-3.0 * t / t60) * np.sin(phi)
    one /= np.max(np.abs(one))
    levels = levels_db if levels_db is not None else [level_db] * n_hits
    out = np.zeros(n_hits * hit_n)
    for i in range(n_hits):
        out[i * hit_n:(i + 1) * hit_n] = _amp(levels[i]) * one
    return out


def bass_808(fs, f_end=45.0, glide_from=90.0, glide_ms=80.0, harm2_db=-8.0, sustain=0.6, level_db=-6.0) -> np.ndarray:
    """SIG-04: one 808 note: glide glide_from -> f_end (time constant glide_ms/3), 2nd harmonic at
    harm2_db, 3 ms attack, flat sustain, then a 150 ms-T60 release (0.2 s tail)."""
    t = np.arange(_n(fs, sustain + 0.2)) / fs
    tau = glide_ms / 1000.0 / 3.0
    phi = 2.0 * np.pi * (f_end * t + (glide_from - f_end) * tau * (1.0 - np.exp(-t / tau)))
    x = np.sin(phi) + 10.0 ** (harm2_db / 20.0) * np.sin(2.0 * phi)
    attack = np.minimum(t / 0.003, 1.0)
    release = np.where(t > sustain, 10.0 ** (-3.0 * (t - sustain) / 0.15), 1.0)
    return _normalize_peak(x * attack * release, level_db)


def bass_pluck(fs, f0, dur=0.8, level_db=-6.0, seed=0, noise="normal") -> np.ndarray:
    """SIG-05: sawtooth low-passed at 400 Hz (4th-order Butterworth), 2 ms attack, 0.6 s T60 decay,
    plus a 5 ms seeded noise burst for the pluck. noise="xorshift" takes the burst from xorshift_noise (seed >= 1)
    instead of a normal generator, so the C++ tests build the same signal (tests/support/Signals: bassPluck)."""
    n = _n(fs, dur)
    t = np.arange(n) / fs
    saw = 2.0 * ((f0 * t) % 1.0) - 1.0
    body = sps.sosfilt(sps.butter(4, 400.0, "low", fs=fs, output="sos"), saw)
    env = np.minimum(t / 0.002, 1.0) * 10.0 ** (-3.0 * t / 0.6)
    burst = np.zeros(n)
    nb = _n(fs, 0.005)
    src = xorshift_noise(nb, seed) if noise == "xorshift" else np.random.default_rng(seed).standard_normal(nb)
    burst[:nb] = 0.1 * src * np.hanning(nb)
    return _normalize_peak(body * env + burst, level_db)


def chord(fs, freqs=(55.0, 69.0, 82.0), dur=2.0, level_db=-12.0) -> np.ndarray:
    t = np.arange(_n(fs, dur)) / fs
    x = sum(np.sin(2.0 * np.pi * f * t) for f in freqs)
    return _normalize_peak(x, level_db)


def two_tone(f1, f2, fs, dur, level_db=-12.0) -> np.ndarray:
    """Two equal sines, each at half of the peak level, so that the sum never exceeds level_db."""
    return 0.5 * sine(f1, fs, dur, level_db) + 0.5 * sine(f2, fs, dur, level_db)


def kick_plus_808(fs) -> np.ndarray:
    """SIG-18: 2 s signal, kick at 0 (-6 dB) plus 808 note at 20 ms (-9 dB), peak-normalised to -3 dBFS."""
    n = _n(fs, 2.0)
    out = kick(fs, n_hits=1, spacing=2.0, level_db=-6.0)[:n].copy()
    note = bass_808(fs, level_db=-9.0)
    off = _n(fs, 0.02)
    m = min(len(note), n - off)
    out[off:off + m] += note[:m]
    return _normalize_peak(out, -3.0)


def pink_noise(fs, dur, f_lo=20.0, f_hi=2000.0, level_db=-24.0, seed=1) -> np.ndarray:
    """SIG-07/16: amplitude spectrum ~ 1/sqrt(f) inside [f_lo, f_hi], exactly zero outside."""
    n = _n(fs, dur)
    spec = np.fft.rfft(np.random.default_rng(seed).standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / fs)
    gain = np.zeros_like(f)
    band = (f >= f_lo) & (f <= f_hi)
    gain[band] = 1.0 / np.sqrt(f[band])
    return _normalize_peak(np.fft.irfft(spec * gain, n), level_db)


_XORSHIFT_MUL = 2685821657736338717  # 0x2545F4914F6CDD1D
_U64 = (1 << 64) - 1


def xorshift_noise(n: int, seed: int = 1) -> np.ndarray:
    """Uniform noise in [-1, 1) from xorshift64*, reproduced bit for bit by the C++ tests (tests/support/Signals):
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27; y = (x * 0x2545F4914F6CDD1D mod 2^64) >> 11; value = y / 2^53 * 2 - 1.
    The state starts at `seed`, which must not be 0."""
    if seed <= 0 or seed > _U64:
        raise ValueError("seed must be in 1 .. 2^64 - 1")
    x = seed
    out = np.empty(n)
    for i in range(n):
        x ^= x >> 12
        x ^= (x << 25) & _U64
        x ^= x >> 27
        out[i] = ((x * _XORSHIFT_MUL) & _U64) >> 11
    return out / 2.0 ** 53 * 2.0 - 1.0


def band_noise(fs, dur, f_lo=20.0, f_hi=2000.0, level_db=-24.0, seed=1) -> np.ndarray:
    """pink_noise with xorshift_noise as the source: amplitude ~ 1/sqrt(f) inside [f_lo, f_hi], exactly zero outside
    (n-point DFT, n = round(fs dur), bin k at k (fs / n)), peak-normalised to level_db. The C++ tests generate the same
    signal (tests/support/Signals: bandNoise)."""
    n = _n(fs, dur)
    if n == 0:
        return np.zeros(0)
    spec = np.fft.rfft(xorshift_noise(n, seed))
    f = np.arange(n // 2 + 1) * (fs / n)
    gain = np.zeros(len(f))
    band = (f >= f_lo) & (f <= f_hi)
    gain[band] = 1.0 / np.sqrt(f[band])
    return _normalize_peak(np.fft.irfft(spec * gain, n), level_db)


def antiphase_pair(fs, f=60.0, dur=2.0, level_db=-12.0):
    left = sine(f, fs, dur, level_db)
    return left, -left


def impulse(n: int, amp: float = 0.5) -> np.ndarray:
    x = np.zeros(n)
    x[0] = amp
    return x


def silence(fs, dur) -> np.ndarray:
    return np.zeros(_n(fs, dur))


def dc(fs, dur, value=0.5) -> np.ndarray:
    return np.full(_n(fs, dur), float(value))


def square(f, fs, dur, level_db=0.0) -> np.ndarray:
    t = np.arange(_n(fs, dur)) / fs
    return _amp(level_db) * np.where(np.sin(2.0 * np.pi * f * t) >= 0.0, 1.0, -1.0)


def nan_inf(n: int) -> np.ndarray:
    """60 Hz sine at 0.25 with a run of 8 NaNs, a single NaN, +inf and -inf injected."""
    x = 0.25 * np.sin(2.0 * np.pi * 60.0 * np.arange(n) / 48000.0)
    x[n // 8:n // 8 + 8] = np.nan
    x[n // 4] = np.inf
    x[n // 2] = -np.inf
    x[3 * n // 4] = np.nan
    return x


def denormal_ramp(n: int) -> np.ndarray:
    """Exponential decay 1e-300 -> 1e-320 (the tail is subnormal in float64)."""
    return 10.0 ** np.linspace(-300.0, -320.0, n)


def sine_in_noise(f, fs, dur, snr_db, level_db=-12.0, seed=2) -> np.ndarray:
    """SIG-19: sine plus white Gaussian noise scaled so that rms(sine)/rms(noise) is exactly snr_db."""
    s = sine(f, fs, dur, level_db)
    noise = np.random.default_rng(seed).standard_normal(len(s))
    noise *= (np.sqrt(np.mean(s * s)) / 10.0 ** (snr_db / 20.0)) / np.sqrt(np.mean(noise * noise))
    return s + noise
