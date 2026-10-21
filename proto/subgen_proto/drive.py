# SPDX-License-Identifier: AGPL-3.0-or-later
"""Drive / Saturate (SPEC §7.3): five characters, even/odd bias, 1st-order ADAA, optional oversampling, Auto Gain.

Domain: x_ref = x * 10^(12/20) (a -12 dBFS-peak sine is +-1.0), pre-gain g = 10^((30 drive/100)/20), blend
alpha = min(1, drive/5): y = ((1 - alpha) x_ref + alpha f(g x_ref)) / 10^(12/20), then the Tone low-pass (wet by
alpha) and the Auto Gain. drive = 0 returns x unchanged. DC left by an asymmetric shaper is removed downstream by
`dc_block` (1-pole 5 Hz, SPEC §4.7); Auto Gain therefore matches the AC RMS.
Fold depth cap (R3, D8 I2): the fold output's bandwidth grows with the slope of g x_ref at its zero crossings, so a hot
input (sub_level before the Drive reaches +18 dB) aliases into the sub band. When the input envelope (hold over
max(10 ms, 1.5 / f_sub1), then a 50 ms release) exceeds the reference -6 dBFS peak (x_ref 2.0), the fold input is
scaled down to it: up to -6 dBFS nothing changes, above it the fold keeps the depth it has at -6 dBFS.
"""
from __future__ import annotations

import functools

import numpy as np
from scipy import signal as sps

from . import envelope, svf, util

REF_DB = 12.0
TUBE_BIAS = 0.35
ADAA_TOL = 1e-5
OS_ATTEN_DB = 110.0     # design stopband of the oversampling filters (kaiserord is ~0.3 dB optimistic; 100 dB is met)
OS_TRANSITION = 0.1     # transition band, as a fraction of the base rate, centred on the base Nyquist frequency
MODES = ("soft", "tube", "tape", "fold", "hard")
FOLD_CAP = 2.0          # x_ref peak of a -6 dBFS input: the fold depth is held there for hotter inputs (R3)
FOLD_RELEASE_MS = 50.0


# ------------------------------------------------------------------------------------------------ shapers
def _log_cosh(x):
    ax = np.abs(x)
    return ax + np.log1p(np.exp(-2.0 * ax)) - np.log(2.0)


_BASE = {  # name -> (f, F1 = integral of f)
    "soft": (np.tanh, _log_cosh),
    "tube": (np.tanh, _log_cosh),  # Tube is Soft with a default bias of 0.35 (see effective_bias)
    "tape": (lambda x: (2.0 / np.pi) * np.arctan(np.pi * x / 2.0),
             lambda x: (2.0 / np.pi) * x * np.arctan(np.pi * x / 2.0) - (2.0 / np.pi ** 2) * np.log1p((np.pi * x / 2.0) ** 2)),
    "fold": (lambda x: np.sin(np.pi * x / 2.0), lambda x: -(2.0 / np.pi) * np.cos(np.pi * x / 2.0)),
    "hard": (lambda x: np.clip(x, -1.0, 1.0), lambda x: np.where(np.abs(x) <= 1.0, 0.5 * x * x, np.abs(x) - 0.5)),
}


def shape_fn(mode: str, bias: float = 0.0):
    """(f, F1) of the shaper with bias b: f_b(x) = f(x + b) - f(b), F1_b(x) = F1(x + b) - x f(b)."""
    f, f1 = _BASE[mode]
    fb = float(f(bias))
    return (lambda x: f(x + bias) - fb), (lambda x: f1(x + bias) - x * fb)


def effective_bias(mode: str, balance: float) -> float:
    """b_eff = b_mode (1 - max(0, bal)/100) + 0.5 max(0, -bal)/100; b_mode = 0.35 for Tube, 0 otherwise."""
    b_mode = TUBE_BIAS if mode == "tube" else 0.0
    return b_mode * (1.0 - max(0.0, balance) / 100.0) + 0.5 * max(0.0, -balance) / 100.0


def _adaa(u, f, f1):
    """y[n] = (F1(u[n]) - F1(u[n-1])) / (u[n] - u[n-1]); f at the midpoint where |du| <= 1e-5 (u[-1] = 0)."""
    prev = np.concatenate([[0.0], u[:-1]])
    du = u - prev
    big = np.abs(du) > ADAA_TOL
    out = np.empty_like(u)
    out[big] = (f1(u[big]) - f1(prev[big])) / du[big]
    out[~big] = f(0.5 * (u[~big] + prev[~big]))
    return out


# ------------------------------------------------------------------------------------------------ oversampling
@functools.lru_cache(maxsize=None)
def oversampling_taps(os_factor: int) -> np.ndarray:
    """Kaiser FIR at the oversampled rate: flat to 0.45 fs, -100 dB from 0.55 fs (fs = the base rate), DC gain 1."""
    width = 2.0 * OS_TRANSITION / os_factor  # in units of the oversampled Nyquist frequency
    numtaps, beta = sps.kaiserord(OS_ATTEN_DB, width)
    return sps.firwin(numtaps | 1, 1.0 / os_factor, window=("kaiser", beta))


# ------------------------------------------------------------------------------------------------ chain
def _gains(drive_pct: float):
    return 10.0 ** ((30.0 * drive_pct / 100.0) / 20.0), min(1.0, drive_pct / 5.0)


def auto_gain_db(mode: str, drive_pct: float, balance: float = 0.0) -> float:
    """Level compensation (dB): AC RMS of the reference sine (1 period, 2048 points, unit amplitude in the shaper
    domain) over the AC RMS of the blended shaper output. Tone, ADAA and oversampling are not part of it."""
    if drive_pct <= 0.0:
        return 0.0
    r = np.sin(2.0 * np.pi * np.arange(2048) / 2048.0)
    g, alpha = _gains(drive_pct)
    f, _ = shape_fn(mode, effective_bias(mode, balance))
    y = (1.0 - alpha) * r + alpha * f(g * r)
    return float(20.0 * np.log10(util.rms(r) / util.rms(y - np.mean(y))))


def fold_scale(x_ref, fs: float, f_sub1: float = 40.0) -> np.ndarray:
    """min(1, FOLD_CAP / envelope): the envelope is a hold of |x_ref| over max(10 ms, 1.5 / f_sub1) followed by a
    one-pole release (instant rise), so a steady tone gets a constant scale and a falling level no step."""
    w = max(1, int(round(max(0.010, 1.5 / f_sub1) * fs)))
    held = envelope.sliding_peak(np.abs(x_ref), w)
    r = np.exp(-1.0 / (FOLD_RELEASE_MS * 1e-3 * fs))
    env = np.empty_like(held)
    e = 0.0
    for i, v in enumerate(held.tolist()):
        e = v if v >= e else max(v, e * r)
        env[i] = e
    return np.minimum(1.0, FOLD_CAP / np.maximum(env, 1e-300))


def process(x, fs: float, *, drive_pct: float, mode: str, balance: float = 0.0, tone_hz=500.0, auto_gain: bool = True,
            os_factor: int = 1, adaa: bool = True, f_sub1: float = 40.0, fold_cap: bool = True) -> np.ndarray:
    """Drive chain on a block x (see the module docstring). tone_hz=None bypasses the Tone filter. The ADAA output
    is delayed by half a (oversampled) sample against the linear path; that delay is part of the dry compensation."""
    x = np.asarray(x, dtype=np.float64)
    if drive_pct <= 0.0:
        return x.copy()
    k = 10.0 ** (REF_DB / 20.0)
    x_ref = x * k
    g, alpha = _gains(drive_pct)
    f, f1 = shape_fn(mode, effective_bias(mode, balance))
    u = g * x_ref
    if mode == "fold" and fold_cap:
        u = u * fold_scale(x_ref, fs, f_sub1)
    if os_factor > 1:
        u = sps.resample_poly(u, os_factor, 1, window=oversampling_taps(os_factor))
    nl = _adaa(u, f, f1) if adaa else f(u)
    if os_factor > 1:
        nl = sps.resample_poly(nl, 1, os_factor, window=oversampling_taps(os_factor))
    y = ((1.0 - alpha) * x_ref + alpha * nl) / k
    if tone_hz is not None:
        y = y + alpha * (svf.process_fast(svf.design_butter(2, tone_hz, fs, "low"), y) - y)
    if auto_gain:
        y = y * 10.0 ** (auto_gain_db(mode, drive_pct, balance) / 20.0)
    return y


def dc_block(y, fs: float, fc: float = 5.0) -> np.ndarray:
    """1-pole high-pass at 5 Hz that removes the DC of an asymmetric shaper (SPEC §4.7)."""
    return svf.process_fast(svf.design_butter(1, fc, fs, "high"), y)


# ------------------------------------------------------------------------------------------------ aliasing
def reference_output(x, fs: float, **params) -> np.ndarray:
    """The alias-free reference of `process`: ADAA at 16x oversampling, advanced by its 1/32 sample delay."""
    ref = process(x, fs, **{**params, "os_factor": 16, "adaa": True})
    return ref


def nonharmonic_db(x, fs: float, f0: float, *, margin_s: float = 0.25, f_hi=None, **params) -> float:
    """Aliasing as TEST_PLAN T-ALIAS-01 defines it: power of the components that are not harmonics of the tone f0,
    over the total power, in 20 Hz ... f_hi (default min(20 kHz, 0.45 fs)). The central part of x (margin_s cut off
    both ends) must hold whole cycles of f0 and f0 should share few factors with fs: an alias that lands on a
    harmonic bin is invisible to this measure (e.g. 80 Hz at 48 kHz: fs / f0 is an integer)."""
    y = process(x, fs, **params)
    m = int(margin_s * fs)
    y = y[m:len(y) - m]
    freqs = np.fft.rfftfreq(len(y), 1.0 / fs)
    keep = (freqs >= 20.0) & (freqs <= (min(20000.0, 0.45 * fs) if f_hi is None else f_hi))
    harm = np.zeros(len(freqs), dtype=bool)
    harm[np.round(np.arange(1, int(0.5 * fs / f0) + 1) * f0 * len(y) / fs).astype(int)] = True
    power = np.abs(np.fft.rfft(y)) ** 2
    return float(10.0 * np.log10(max(np.sum(power[keep & ~harm]), 1e-300) / np.sum(power[keep])))


def alias_error_db(x, fs: float, *, margin_s: float = 0.25, ref=None, **params) -> float:
    """RMS of (process - reference) over the RMS of the reference, in dB, band-limited to 20 Hz ... min(20 kHz,
    0.45 fs). Includes the high-frequency droop of ADAA's 2-tap average (-58 dB for Hard at 100 %), so it is not an
    aliasing measure by itself: see nonharmonic_db. The central part of x (margin_s cut off both ends) must hold
    whole cycles of the tone. The ADAA delay (half a sample at the oversampled rate) is removed from both outputs
    by a periodic fractional advance."""
    os_factor = params.get("os_factor", 1)
    y = process(x, fs, **params)
    ref = reference_output(x, fs, **params) if ref is None else ref
    m = int(margin_s * fs)
    y, ref = y[m:len(y) - m], ref[m:len(ref) - m]
    if params.get("adaa", True):
        y = util.frac_delay(y, -0.5 / os_factor)
    ref = util.frac_delay(ref, -0.5 / 16.0)
    freqs = np.fft.rfftfreq(len(y), 1.0 / fs)
    keep = (freqs >= 20.0) & (freqs <= min(20000.0, 0.45 * fs))
    spec = lambda a: np.fft.rfft(a)[keep]
    err, sig = np.sqrt(np.sum(np.abs(spec(y) - spec(ref)) ** 2)), np.sqrt(np.sum(np.abs(spec(ref)) ** 2))
    return float(max(20.0 * np.log10(max(err, 1e-300) / sig), -300.0))
