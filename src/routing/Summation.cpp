// SPDX-License-Identifier: AGPL-3.0-or-later
#include "routing/Summation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/SampleRate.h"

namespace subgen::routing
{
namespace
{
constexpr double kSmoothMs = 20.0;
constexpr double kModeMs = 20.0;
constexpr double kWetMs = 10.0;

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double finiteGain(double g) { return std::isfinite(g) ? std::clamp(g, 0.0, 16.0) : 0.0; }

// the Mix law's dry and sub coefficients (SPEC §9.1)
void mixLaw(double m, bool equalPower, double& a, double& b)
{
    if (m <= 0.0)
    {
        a = 1.0;
        b = 0.0;
    }
    else if (m >= 1.0 - 1e-12)
    {
        a = 0.0;
        b = 1.0;
    }
    else if (equalPower)
    {
        a = std::cos(0.5 * std::numbers::pi * m);
        b = std::sin(0.5 * std::numbers::pi * m);
    }
    else
    {
        a = 1.0 - m;
        b = m;
    }
}
} // namespace

void Summation::prepare(double sampleRate)
{
    fs = sanitizeSampleRate(sampleRate);
    reset();
}

void Summation::reset() { fresh = true; }

void Summation::setParams(const SumParams& p)
{
    params = p;
    const double m = std::isfinite(p.mix) ? std::clamp(p.mix, 0.0, 1.0) : 0.0;
    if (fresh)
    {
        fresh = false;
        dryGain.reset(fs, kSmoothMs, finiteGain(p.dryGain));
        mix.reset(fs, kSmoothMs, m);
        mode.reset(fs, kModeMs, p.mixMode ? 1.0 : 0.0);
        wet.reset(fs, kWetMs, p.wetOnly ? 1.0 : 0.0);
        trim.reset(fs, kSmoothMs, finiteGain(p.outTrimGain));
        return;
    }
    dryGain.setTarget(finiteGain(p.dryGain));
    mix.setTarget(m);
    mode.setTarget(p.mixMode ? 1.0 : 0.0);
    wet.setTarget(p.wetOnly ? 1.0 : 0.0);
    trim.setTarget(finiteGain(p.outTrimGain));
}

void Summation::process(const double* const* dry, const double* const* sub, double* const* out, int numCh, int n)
{
    for (int i = 0; i < n; ++i)
    {
        double a = 1.0, b = 0.0;
        mixLaw(mix.next(), params.equalPower, a, b);
        const double addDry = dryGain.next();
        const double w = sCurve(mode.next());
        double dryCoeff = w == 0.0 ? addDry : (w == 1.0 ? a : (1.0 - w) * addDry + w * a);
        const double subCoeff = w == 0.0 ? 1.0 : (w == 1.0 ? b : (1.0 - w) + w * b);
        const double wetOnly = sCurve(wet.next());
        if (wetOnly != 0.0)
            dryCoeff = wetOnly == 1.0 ? 0.0 : dryCoeff * (1.0 - wetOnly);
        const double g = trim.next();
        for (int c = 0; c < numCh; ++c)
        {
            const double d = dry[c][i];
            const double u = std::isfinite(sub[c][i]) ? sub[c][i] : 0.0;
            const double mixed = subCoeff == 0.0 ? dryCoeff * d : (dryCoeff == 0.0 ? subCoeff * u : dryCoeff * d + subCoeff * u);
            out[c][i] = g == 1.0 ? mixed : g * mixed;
        }
    }
}
} // namespace subgen::routing
