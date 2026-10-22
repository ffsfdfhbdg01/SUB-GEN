// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/Signals.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "support/Dft.h"

namespace subgen::test::signals
{
namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;

double amp(double levelDb)
{
    return std::pow(10.0, levelDb / 20.0);
}

std::size_t length(double fs, double dur)
{
    return static_cast<std::size_t>(std::nearbyint(fs * dur)); // ties to even, as Python's round()
}

void normalizePeak(std::vector<double>& x, double levelDb)
{
    double peak = 0.0;
    for (const double v : x)
        peak = std::max(peak, std::abs(v));
    if (peak == 0.0)
        return;
    const double factor = amp(levelDb) / peak;
    for (auto& v : x)
        v *= factor;
}
} // namespace

std::vector<double> sine(double f, double fs, double dur, double levelDb, double phase)
{
    std::vector<double> x(length(fs, dur));
    const double a = amp(levelDb);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = a * std::sin(kTwoPi * f * (static_cast<double>(i) / fs) + phase);
    return x;
}

std::vector<double> logSweep(double f0, double f1, double fs, double dur, double levelDb)
{
    std::vector<double> x(length(fs, dur));
    const double r = f1 / f0;
    const double scale = kTwoPi * f0 * dur / std::log(r);
    const double a = amp(levelDb);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double>(i) / fs;
        x[i] = a * std::sin(scale * (std::pow(r, t / dur) - 1.0));
    }
    return x;
}

std::vector<double> kick(double fs, const KickParams& p)
{
    const std::size_t hitN = length(fs, p.spacing);
    std::vector<double> one(hitN);
    double peak = 0.0;
    for (std::size_t i = 0; i < hitN; ++i)
    {
        const double t = static_cast<double>(i) / fs;
        const double phi =
            kTwoPi * (p.fEnd * t + (p.fStart - p.fEnd) * p.tauPitch * (1.0 - std::exp(-t / p.tauPitch))) + p.phase;
        one[i] = std::pow(10.0, -3.0 * t / p.t60) * std::sin(phi);
        peak = std::max(peak, std::abs(one[i]));
    }
    for (auto& v : one)
        v /= peak;
    std::vector<double> out(static_cast<std::size_t>(p.nHits) * hitN, 0.0);
    for (int h = 0; h < p.nHits; ++h)
    {
        const double a = amp(p.levelsDb.empty() ? p.levelDb : p.levelsDb.at(static_cast<std::size_t>(h)));
        for (std::size_t i = 0; i < hitN; ++i)
            out[static_cast<std::size_t>(h) * hitN + i] = a * one[i];
    }
    return out;
}

std::vector<double> bass808(double fs, const Bass808Params& p)
{
    std::vector<double> x(length(fs, p.sustain + 0.2));
    const double tau = p.glideMs / 1000.0 / 3.0;
    const double h2 = std::pow(10.0, p.harm2Db / 20.0);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double>(i) / fs;
        const double phi = kTwoPi * (p.fEnd * t + (p.glideFrom - p.fEnd) * tau * (1.0 - std::exp(-t / tau)));
        const double attack = std::min(t / 0.003, 1.0);
        const double release = t > p.sustain ? std::pow(10.0, -3.0 * (t - p.sustain) / 0.15) : 1.0;
        x[i] = (std::sin(phi) + h2 * std::sin(2.0 * phi)) * attack * release;
    }
    normalizePeak(x, p.levelDb);
    return x;
}

std::vector<double> twoTone(double f1, double f2, double fs, double dur, double levelDb)
{
    auto a = sine(f1, fs, dur, levelDb);
    const auto b = sine(f2, fs, dur, levelDb);
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = 0.5 * a[i] + 0.5 * b[i];
    return a;
}

std::vector<double> square(double f, double fs, double dur, double levelDb)
{
    std::vector<double> x(length(fs, dur));
    const double a = amp(levelDb);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = a * (std::sin(kTwoPi * f * (static_cast<double>(i) / fs)) >= 0.0 ? 1.0 : -1.0);
    return x;
}

std::vector<double> impulse(std::size_t n, double amp)
{
    std::vector<double> x(n, 0.0);
    if (n > 0)
        x[0] = amp;
    return x;
}

std::vector<double> silence(double fs, double dur)
{
    return std::vector<double>(length(fs, dur), 0.0);
}

std::vector<double> dc(double fs, double dur, double value)
{
    return std::vector<double>(length(fs, dur), value);
}

std::vector<double> nanInf(std::size_t n)
{
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = 0.25 * std::sin(kTwoPi * 60.0 * static_cast<double>(i) / 48000.0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (std::size_t i = n / 8; i < std::min(n, n / 8 + 8); ++i)
        x[i] = nan;
    if (n >= 4)
    {
        x[n / 4] = inf;
        x[n / 2] = -inf;
        x[3 * n / 4] = nan;
    }
    return x;
}

std::vector<double> denormalRamp(std::size_t n)
{
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double e = n == 1 ? -300.0 : -300.0 + static_cast<double>(i) * (-20.0 / static_cast<double>(n - 1));
        x[i] = std::pow(10.0, i + 1 == n ? -320.0 : e);
    }
    return x;
}

std::vector<double> xorshiftNoise(std::size_t n, std::uint64_t seed)
{
    if (seed == 0)
        throw std::invalid_argument("xorshiftNoise: seed must not be 0");
    std::vector<double> out(n);
    std::uint64_t x = seed;
    for (auto& v : out)
    {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        const std::uint64_t y = (x * 2685821657736338717ULL) >> 11;
        v = static_cast<double>(y) / 9007199254740992.0 * 2.0 - 1.0; // y / 2^53 * 2 - 1
    }
    return out;
}

std::vector<double> bandNoise(double fs, double dur, double fLo, double fHi, double levelDb, std::uint64_t seed)
{
    const std::size_t n = length(fs, dur);
    if (n == 0)
        return {};
    auto spec = rdft(xorshiftNoise(n, seed));
    for (std::size_t k = 0; k < spec.size(); ++k)
    {
        const double f = static_cast<double>(k) * (fs / static_cast<double>(n));
        spec[k] *= (f >= fLo && f <= fHi) ? 1.0 / std::sqrt(f) : 0.0;
    }
    auto x = irdft(spec, n);
    normalizePeak(x, levelDb);
    return x;
}
} // namespace subgen::test::signals
