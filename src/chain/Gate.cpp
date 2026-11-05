// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/Gate.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/Quality.h"

namespace subgen::chain
{
namespace
{
constexpr double kHysteresisDb = 3.0;
constexpr double kAttackMs = 2.0;
constexpr double kSmoothMs = 20.0;
constexpr double kSwitchMs = 10.0;

double coefficient(double ms, double fs) { return 1.0 - std::exp(-1.0 / (ms * 1e-3 * fs)); }

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); } // a raised-cosine switch

double finiteOr(double v, double fallback, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }
} // namespace

void Gate::prepare(double sampleRate, int)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    detector.prepare(fs, static_cast<int>(std::ceil(0.075 * fs)) + 1);
    reset();
}

void Gate::reset()
{
    detector.reset();
    below = 0;
    open = false;
    gain = 0.0;
    started = false;
    fresh = true;
}

void Gate::setParams(const GateParams& p)
{
    params = p;
    params.threshDb = finiteOr(p.threshDb, -50.0, -120.0, 0.0);
    params.rangeDb = finiteOr(p.rangeDb, -40.0, -120.0, 0.0);
    params.holdMs = finiteOr(p.holdMs, 40.0, 0.0, 5000.0);
    params.releaseMs = finiteOr(p.releaseMs, 120.0, 1.0, 5000.0);
    params.fSub1Hz = finiteOr(p.fSub1Hz, 40.0, 20.0, 1000.0);
    detector.set(Detector::Peak, params.fSub1Hz);
    hold = msToSamples(params.holdMs, fs);
    const double floorMs = 250.0 / params.fSub1Hz; // a quarter of the sub period
    opening = coefficient(std::max(kAttackMs, floorMs), fs);
    closing = coefficient(std::max(params.releaseMs, floorMs), fs);
    if (fresh)
    {
        fresh = false;
        thresh.reset(fs, kSmoothMs, params.threshDb);
        range.reset(fs, kSmoothMs, params.rangeDb);
        wet.reset(fs, kSwitchMs, params.on ? 1.0 : 0.0);
        return;
    }
    thresh.setTarget(params.threshDb);
    range.setTarget(params.rangeDb);
    wet.setTarget(params.on ? 1.0 : 0.0);
}

void Gate::process(std::span<double> block)
{
    for (auto& x : block)
    {
        const double env = detector.processDb(x);
        const double th = thresh.next();
        if (!open)
        {
            if (env >= th)
            {
                open = true;
                below = 0;
            }
        }
        else if (env < th - kHysteresisDb)
        {
            if (++below > hold)
                open = false;
        }
        else
            below = 0;
        const double target = open ? 0.0 : range.next();
        if (open)
            range.next();
        if (!started)
        {
            gain = target;
            started = true;
        }
        else
            gain += (target > gain ? opening : closing) * (target - gain);
        const double w = sCurve(wet.next());
        if (w != 0.0)
        {
            const double gated = x * std::pow(10.0, gain / 20.0);
            x = w == 1.0 ? gated : w * gated + (1.0 - w) * x;
        }
    }
}
} // namespace subgen::chain
