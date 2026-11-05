// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/Boost.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::chain
{
namespace
{
constexpr double kSmoothMs = 20.0;

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double finiteOr(double v, double fallback, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }

// w a + (1 - w) b, and exactly a when both paths agree (0 dB stays bit-exact through a crossfade)
double mix(double w, double a, double b) { return a == b ? a : w * a + (1.0 - w) * b; }
} // namespace

void Boost::prepare(double sampleRate)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void Boost::reset()
{
    for (auto& s : sections)
        s.reset();
    fading = false;
    fresh = true;
    heardSub = false;
    madeGain = madeCentre = madeQ = -1.0;
}

void Boost::setParams(const BoostParams& p)
{
    const double gainLin = std::pow(10.0, finiteOr(p.gainDb, 0.0, 0.0, 24.0) / 20.0);
    const bool following = p.follow && p.engineHasPitch;
    double fc = finiteOr(p.freqHz, 55.0, 10.0, 0.45 * fs);
    if (following && std::isfinite(p.fSub1Hz) && p.fSub1Hz > 0.0)
    {
        fc = std::clamp(p.fSub1Hz, 10.0, 0.45 * fs);
        heardSub = true;
    }
    else if (following && heardSub)
        fc = centreTarget;
    centreTarget = fc;
    const double qq = finiteOr(p.q, 1.0, 0.1, 20.0);
    if (fresh)
    {
        fresh = false;
        gain.reset(fs, kSmoothMs, gainLin);
        centre.reset(fs, kSmoothMs, fc);
        q.reset(fs, kSmoothMs, qq);
        fade.reset(fs, kSmoothMs, 1.0);
        requested = current = previous = p.type;
        updateCoefficients(gainLin, fc, qq);
        return;
    }
    gain.setTarget(gainLin);
    centre.setTarget(fc);
    q.setTarget(qq);
    requested = p.type;
}

void Boost::updateCoefficients(double g, double fc, double qq)
{
    const double db = 20.0 * std::log10(g); // exactly 0 at a gain of 1: the identity
    sections[0].set(dsp::makeSvf(dsp::SvfMode::Bell, fc, qq, db, fs));
    sections[1].set(dsp::makeSvf(dsp::SvfMode::LowShelf, fc, qq, db, fs));
    madeGain = g;
    madeCentre = fc;
    madeQ = qq;
}

void Boost::process(std::span<double> block)
{
    if (fresh)
        return;
    for (auto& x : block)
    {
        const double g = gain.next();
        const double fc = centre.next();
        const double qq = q.next();
        if (g != madeGain || fc != madeCentre || qq != madeQ)
            updateCoefficients(g, fc, qq);
        if (!fading && requested != current)
        {
            previous = current;
            current = requested;
            fade.reset(fs, kSmoothMs, 0.0);
            fade.setTarget(1.0);
            fading = true;
        }
        const double peak = sections[0].process(x);
        const double shelf = sections[1].process(x);
        const double now = current == BoostType::Peak ? peak : shelf;
        if (fading)
        {
            x = mix(sCurve(fade.next()), now, previous == BoostType::Peak ? peak : shelf);
            fading = fade.isSmoothing();
        }
        else
            x = now;
    }
}
} // namespace subgen::chain
