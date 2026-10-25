// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/Eq.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace subgen::chain
{
namespace
{
constexpr double kSmoothMs = 20.0;
constexpr double kOnMs = 10.0;
constexpr double kTypeMs = 20.0;

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double finiteOr(double v, double fallback, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }

// w a + (1 - w) b, and exactly a when both paths agree (0 dB stays bit-exact through a crossfade)
double mix(double w, double a, double b) { return a == b ? a : w * a + (1.0 - w) * b; }

constexpr std::array<dsp::SvfMode, 3> kModes{dsp::SvfMode::Bell, dsp::SvfMode::LowShelf, dsp::SvfMode::HighShelf};
} // namespace

void Eq::prepare(double sampleRate)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void Eq::reset()
{
    for (auto& b : bands)
    {
        for (auto& s : b.sections)
            s.reset();
        b.typeFading = false;
        b.madeGain = b.madeFreq = b.madeQ = -1.0;
    }
    fresh = true;
}

void Eq::updateCoefficients(Band& b, double g, double f, double qq) const
{
    const double db = 20.0 * std::log10(g); // exactly 0 at a gain of 1: the identity
    for (size_t t = 0; t < kModes.size(); ++t)
        b.sections[t].set(dsp::makeSvf(kModes[t], f, qq, db, fs));
    b.madeGain = g;
    b.madeFreq = f;
    b.madeQ = qq;
}

void Eq::setParams(const EqParams& p)
{
    for (size_t i = 0; i < bands.size(); ++i)
    {
        Band& b = bands[i];
        const EqBand& in = p.bands[i];
        const double g = std::pow(10.0, finiteOr(in.gainDb, 0.0, -24.0, 24.0) / 20.0);
        const double f = finiteOr(in.freqHz, 100.0, 10.0, 0.45 * fs);
        const double qq = finiteOr(in.q, 1.0, 0.1, 20.0);
        const EqType type = static_cast<size_t>(in.type) < kModes.size() ? in.type : EqType::Bell; // a stray value
        if (fresh)
        {
            b.gain.reset(fs, kSmoothMs, g);
            b.freq.reset(fs, kSmoothMs, f);
            b.q.reset(fs, kSmoothMs, qq);
            b.onFade.reset(fs, kOnMs, in.on ? 1.0 : 0.0);
            b.typeFade.reset(fs, kTypeMs, 1.0);
            b.requested = b.current = b.previous = type;
            b.on = in.on;
            updateCoefficients(b, g, f, qq);
            continue;
        }
        b.gain.setTarget(g);
        b.freq.setTarget(f);
        b.q.setTarget(qq);
        b.onFade.setTarget(in.on ? 1.0 : 0.0);
        b.on = in.on;
        b.requested = type;
    }
    fresh = false;
}

void Eq::process(std::span<double> block)
{
    if (fresh)
        return;
    for (auto& x : block)
        for (auto& b : bands)
        {
            const double g = b.gain.next();
            const double f = b.freq.next();
            const double qq = b.q.next();
            if (g != b.madeGain || f != b.madeFreq || qq != b.madeQ)
                updateCoefficients(b, g, f, qq);
            if (!b.typeFading && b.requested != b.current)
            {
                b.previous = b.current;
                b.current = b.requested;
                b.typeFade.reset(fs, kTypeMs, 0.0);
                b.typeFade.setTarget(1.0);
                b.typeFading = true;
            }
            std::array<double, 3> y{};
            for (size_t t = 0; t < y.size(); ++t)
                y[t] = b.sections[t].process(x);
            double shaped = y[static_cast<size_t>(b.current)];
            if (b.typeFading)
            {
                shaped = mix(sCurve(b.typeFade.next()), shaped, y[static_cast<size_t>(b.previous)]);
                b.typeFading = b.typeFade.isSmoothing();
            }
            const double w = sCurve(b.onFade.next());
            x = w == 1.0 ? shaped : (w == 0.0 ? x : mix(w, shaped, x));
        }
}

double Eq::curveDb(double fHz) const
{
    double sum = 0.0;
    for (const auto& b : bands)
        if (b.on)
            sum += 20.0 * std::log10(std::abs(b.sections[static_cast<size_t>(b.current)].response(fHz, fs)));
    return sum;
}
} // namespace subgen::chain
