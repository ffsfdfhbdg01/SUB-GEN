// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/Compressor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::chain
{
namespace
{
constexpr double kLookaheadMs = 5.0; // Print
constexpr double kAutoFastMs = 50.0;
constexpr double kAutoSlowAttackMs = 150.0;
constexpr double kAutoSlowMs = 500.0;
constexpr double kSmoothMs = 20.0;
constexpr double kSwitchMs = 10.0;

double coefficient(double ms, double fs) { return 1.0 - std::exp(-1.0 / (ms * 1e-3 * fs)); }

// raised cosine: no slope step at either end of the switch (a linear ramp's corners showed when the levels differ)
double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double finiteOr(double v, double fallback, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }
} // namespace

double staticGainReductionDb(double levelDb, double threshDb, double ratio, double kneeDb)
{
    const double d = levelDb - threshDb;
    double out = levelDb;
    if (kneeDb <= 0.0)
        out = d > 0.0 ? threshDb + d / ratio : levelDb;
    else if (2.0 * d < -kneeDb)
        out = levelDb;
    else if (2.0 * d > kneeDb)
        out = threshDb + d / ratio;
    else
        out = levelDb + (1.0 / ratio - 1.0) * (d + kneeDb / 2.0) * (d + kneeDb / 2.0) / (2.0 * kneeDb);
    return out - levelDb;
}

void Compressor::prepare(double sampleRate, int, Quality quality)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    lookahead = quality == Quality::Print ? msToSamples(kLookaheadMs, fs) : 0;
    delay.prepare(lookahead);
    delay.setDelay(lookahead);
    detector.prepare(fs, static_cast<int>(std::ceil(0.075 * fs)) + 1); // 1.5 periods of 20 Hz
    reset();
}

void Compressor::reset()
{
    detector.reset();
    delay.reset();
    single = fast = slow = reduction = 0.0;
    fresh = true;
}

void Compressor::setParams(const CompParams& p)
{
    params = p;
    params.threshDb = finiteOr(p.threshDb, -18.0, -60.0, 0.0);
    params.ratio = finiteOr(p.ratio, 3.0, 1.0, 100.0);
    params.kneeDb = finiteOr(p.kneeDb, 6.0, 0.0, 24.0);
    params.attackMs = finiteOr(p.attackMs, 10.0, 0.01, 1000.0);
    params.releaseMs = finiteOr(p.releaseMs, 150.0, 1.0, 5000.0);
    params.makeupDb = finiteOr(p.makeupDb, 0.0, -24.0, 36.0);
    params.fSub1Hz = finiteOr(p.fSub1Hz, 40.0, 20.0, 1000.0);
    detector.set(params.detector, params.fSub1Hz);
    double attackMs = params.attackMs;
    if (params.detector == Detector::Rms) // R4: a quarter of the sub period at least
        attackMs = std::max(attackMs, 250.0 / params.fSub1Hz);
    attack = coefficient(attackMs, fs);
    release = coefficient(params.releaseMs, fs);
    fastRelease = coefficient(kAutoFastMs, fs);
    slowAttack = coefficient(std::max(attackMs, kAutoSlowAttackMs), fs);
    slowRelease = coefficient(kAutoSlowMs, fs);
    const double makeupDb =
        params.makeupDb + (params.autoMakeup ? 0.5 * std::abs(params.threshDb) * (1.0 - 1.0 / params.ratio) : 0.0);
    const double makeupGain = std::pow(10.0, makeupDb / 20.0);
    if (fresh)
    {
        fresh = false;
        thresh.reset(fs, kSmoothMs, params.threshDb);
        ratio.reset(fs, kSmoothMs, params.ratio);
        knee.reset(fs, kSmoothMs, params.kneeDb);
        makeup.reset(fs, kSmoothMs, makeupGain);
        wet.reset(fs, kSwitchMs, params.on ? 1.0 : 0.0);
        return;
    }
    thresh.setTarget(params.threshDb);
    ratio.setTarget(params.ratio);
    knee.setTarget(params.kneeDb);
    makeup.setTarget(makeupGain);
    wet.setTarget(params.on ? 1.0 : 0.0);
}

void Compressor::process(std::span<double> block)
{
    for (auto& x : block)
    {
        // the reduction runs while off as well, so switching on starts from the right gain
        const double level = detector.processDb(x);
        const double target = staticGainReductionDb(level, thresh.next(), ratio.next(), knee.next());
        if (params.autoRelease)
        {
            fast += (target < fast ? attack : fastRelease) * (target - fast);
            slow += (target < slow ? slowAttack : slowRelease) * (target - slow);
            reduction = std::min(fast, slow);
        }
        else
        {
            single += (target < single ? attack : release) * (target - single);
            reduction = single;
        }
        const double gain = std::pow(10.0, reduction / 20.0) * makeup.next();
        const double w = sCurve(wet.next());
        const double dry = delay.process(x);
        x = w == 0.0 ? dry : w * (dry * gain) + (1.0 - w) * dry;
    }
}
} // namespace subgen::chain
