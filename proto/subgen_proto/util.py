# SPDX-License-Identifier: AGPL-3.0-or-later
"""Measurement helpers for the SUB-GEN DSP prototype (double precision, deterministic)."""
from __future__ import annotations

import numpy as np
from scipy import signal as sps
from scipy.signal import windows

_FLOOR = 1e-300


def db(x):
    """20*log10(|x|), floored so that zeros give -6000 dB instead of -inf."""
    return 20.0 * np.log10(np.maximum(np.abs(np.asarray(x, dtype=np.float64)), _FLOOR))


def undb(d):
    return 10.0 ** (np.asarray(d, dtype=np.float64) / 20.0)


def rms(x) -> float:
    x = np.asarray(x, dtype=np.float64)
    return float(np.sqrt(np.mean(x * x)))


def null_db(ref, test) -> float:
    """20*log10(rms(ref - test) / rms(ref)); an exact null returns the floor of -400 dB."""
    ref = np.asarray(ref, dtype=np.float64)
    test = np.asarray(test, dtype=np.float64)
    num = rms(ref - test)
    den = rms(ref)
    if num == 0.0:
        return -400.0
    if den == 0.0:
        return float("inf")
    return max(-400.0, float(20.0 * np.log10(num / den)))


def peak_freq(x, fs: float, f_lo: float, f_hi: float, pad: int = 16):
    """Frequency (Hz) and amplitude (linear, peak) of the strongest spectral peak in [f_lo, f_hi].

    Blackman-Harris window, zero padding by at least `pad`x, parabolic interpolation of the
    log-magnitude around the maximum.
    """
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    w = windows.blackmanharris(n, sym=False)
    nfft = int(2 ** np.ceil(np.log2(n * pad)))
    spec = np.fft.rfft(x * w, nfft)
    mag = np.abs(spec) * 2.0 / np.sum(w)
    freqs = np.fft.rfftfreq(nfft, 1.0 / fs)
    idx = np.flatnonzero((freqs >= f_lo) & (freqs <= f_hi))
    if idx.size == 0:
        return 0.0, 0.0
    k = int(idx[np.argmax(mag[idx])])
    if 1 <= k < len(mag) - 1:
        a = np.log(mag[k - 1] + _FLOOR)
        b = np.log(mag[k] + _FLOOR)
        c = np.log(mag[k + 1] + _FLOOR)
        denom = a - 2.0 * b + c
        delta = 0.5 * (a - c) / denom if denom != 0.0 else 0.0
        return float((k + delta) * fs / nfft), float(np.exp(b - 0.25 * (a - c) * delta))
    return float(freqs[k]), float(mag[k])


def zero_crossings(x, rising: bool = True) -> np.ndarray:
    """Fractional sample positions of rising (x[n-1] < 0 <= x[n]) or falling (x[n-1] > 0 >= x[n]) crossings."""
    x = np.asarray(x, dtype=np.float64)
    if rising:
        idx = np.flatnonzero((x[:-1] < 0.0) & (x[1:] >= 0.0))
    else:
        idx = np.flatnonzero((x[:-1] > 0.0) & (x[1:] <= 0.0))
    frac = x[idx] / (x[idx] - x[idx + 1])
    return idx + frac


def zc_freq(x, fs: float) -> float:
    """Mean frequency from the first to the last rising zero crossing (0.0 if fewer than two)."""
    z = zero_crossings(x, rising=True)
    if len(z) < 2:
        return 0.0
    return float(fs * (len(z) - 1) / (z[-1] - z[0]))


def harmonic_amps(x, fs: float, f0: float, n: int = 10) -> np.ndarray:
    """Peak amplitudes at k*f0 (k = 1..n), each searched within +/-3 % of k*f0; 0 above Nyquist."""
    out = np.zeros(n)
    for k in range(1, n + 1):
        fk = k * f0
        if fk * 1.03 >= fs / 2.0:
            break
        out[k - 1] = peak_freq(x, fs, fk * 0.97, fk * 1.03)[1]
    return out


def thd_db(x, fs: float, f0: float, n: int = 10) -> float:
    amps = harmonic_amps(x, fs, f0, n)
    ratio = np.sqrt(np.sum(amps[1:] ** 2)) / max(amps[0], _FLOOR)
    return float(20.0 * np.log10(max(ratio, _FLOOR)))


def band_db(x, fs: float, f1: float, f2: float) -> float:
    """RMS level (dBFS) of the part of x between f1 and f2 (rectangular DFT, Parseval).

    Exact for signals with integer cycles in the record or band-limited noise.
    """
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    spec = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(n, 1.0 / fs)
    weight = np.full(len(spec), 2.0)
    weight[0] = 1.0
    if n % 2 == 0:
        weight[-1] = 1.0
    sel = (freqs >= f1) & (freqs <= f2)
    power = np.sum(weight[sel] * np.abs(spec[sel]) ** 2) / (n * n)
    return float(20.0 * np.log10(max(np.sqrt(power), 1e-15)))


def frac_delay(x, d: float) -> np.ndarray:
    """y[n] = x(n - d) by FFT phase shift; exact for periodic signals (circular)."""
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    spec = np.fft.rfft(x)
    k = np.arange(len(spec))
    phase = np.exp(-2j * np.pi * k * d / n)
    if n % 2 == 0:
        phase[-1] = np.cos(np.pi * d)
    return np.fft.irfft(spec * phase, n)


def a_weight_sos(fs: float) -> np.ndarray:
    """IEC 61672 A-weighting as digital SOS (bilinear, normalised to 0 dB at 1 kHz)."""
    f1, f2, f3, f4 = 20.598997, 107.65265, 737.86223, 12194.217
    zeros = np.zeros(4)
    poles = -2.0 * np.pi * np.array([f1, f1, f4, f4, f2, f3])
    gain = (2.0 * np.pi * f4) ** 2
    zd, pd, kd = sps.bilinear_zpk(zeros, poles, gain, fs)
    sos = sps.zpk2sos(zd, pd, kd)
    _, h = sps.sosfreqz(sos, worN=[1000.0], fs=fs)
    sos[0, :3] /= abs(h[0])
    return sos


def loudness_a_db(x, fs: float) -> float:
    """RMS level (dB) of the A-weighted signal; the first 100 ms (filter transient) are skipped."""
    y = sps.sosfilt(a_weight_sos(fs), np.asarray(x, dtype=np.float64))
    skip = min(len(y) // 10, int(0.1 * fs))
    return float(20.0 * np.log10(max(rms(y[skip:]), _FLOOR)))
