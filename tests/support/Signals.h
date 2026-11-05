// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Test signals with the parameters, defaults and arithmetic of the prototype's signals.py (proto/subgen_proto), so
// the C++ tests run on the same inputs as the reference (checked against tests/golden/signals.sgg). `levelDb` is the
// peak level in dBFS; lengths are round(fs dur) with ties to even, like Python's round().
namespace subgen::test::signals
{
std::vector<double> sine(double f, double fs, double dur, double levelDb = -6.0, double phase = 0.0);
std::vector<double> logSweep(double f0, double f1, double fs, double dur, double levelDb = -12.0);

// SIG-03: sine with an exponential pitch drop fStart -> fEnd (time constant tauPitch) and an exponential decay
// reaching -60 dB after t60; each hit peak-normalised to its own level (levelsDb, or levelDb for all).
struct KickParams
{
    int nHits = 1;
    double spacing = 0.5;
    double levelDb = -6.0;
    std::vector<double> levelsDb{};
    double fStart = 150.0;
    double fEnd = 45.0;
    double tauPitch = 0.030;
    double t60 = 0.300;
    double phase = 0.0;
};
std::vector<double> kick(double fs, const KickParams& p = {});

// SIG-04: one 808 note: glide glideFrom -> fEnd (time constant glideMs/3), 2nd harmonic at harm2Db, 3 ms attack,
// flat sustain, then a 150 ms-T60 release (0.2 s tail).
struct Bass808Params
{
    double fEnd = 45.0;
    double glideFrom = 90.0;
    double glideMs = 80.0;
    double harm2Db = -8.0;
    double sustain = 0.6;
    double levelDb = -6.0;
};
std::vector<double> bass808(double fs, const Bass808Params& p = {});

// SIG-05 as signals.bass_pluck(noise="xorshift"): sawtooth low-passed at 400 Hz (4th-order Butterworth), 2 ms attack,
// 0.6 s T60 decay, plus a 5 ms Hann-windowed xorshift burst for the pluck.
struct BassPluckParams
{
    double dur = 0.8;
    double levelDb = -6.0;
    std::uint64_t seed = 1;
};
std::vector<double> bassPluck(double fs, double f0, const BassPluckParams& p = {});

std::vector<double> twoTone(double f1, double f2, double fs, double dur, double levelDb = -12.0);
std::vector<double> square(double f, double fs, double dur, double levelDb = 0.0);
std::vector<double> impulse(std::size_t n, double amp = 0.5);
std::vector<double> silence(double fs, double dur);
std::vector<double> dc(double fs, double dur, double value = 0.5);
std::vector<double> nanInf(std::size_t n);       // 60 Hz sine at 0.25 with NaN runs and +-inf injected
std::vector<double> denormalRamp(std::size_t n); // 1e-300 -> 1e-320

// xorshift64* uniform noise in [-1, 1); seed must not be 0.
std::vector<double> xorshiftNoise(std::size_t n, std::uint64_t seed = 1);

// pink_noise from xorshiftNoise: amplitude ~ 1/sqrt(f) inside [fLo, fHi], exactly zero outside, peak levelDb.
std::vector<double> bandNoise(double fs, double dur, double fLo = 20.0, double fHi = 2000.0, double levelDb = -24.0,
                              std::uint64_t seed = 1);
} // namespace subgen::test::signals
