// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/Limiter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "chain/DriveShapes.h"

namespace subgen::chain
{
namespace
{
constexpr double kAdaaTolerance = 1e-5;
constexpr double kSmoothMs = 20.0;
constexpr double kSwitchMs = 10.0;

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double besselI0(double x) // series; converges fast for the window's arguments
{
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-17 * sum)
            break;
    }
    return sum;
}

// taps[p][j] = h[4 j + p] of the 64-tap design, each phase summing to 1
const std::array<std::array<double, TruePeakInterpolator::kTapsPerPhase>, 4>& interpolationTaps()
{
    static const auto taps = [] {
        constexpr int n = 4 * TruePeakInterpolator::kTapsPerPhase;
        constexpr double beta = 8.0;
        std::array<std::array<double, TruePeakInterpolator::kTapsPerPhase>, 4> t{};
        for (int i = 0; i < n; ++i)
        {
            const double x = (i - 0.5 * (n - 1)) / 4.0; // base samples from the centre
            const double sinc = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double r = 2.0 * i / (n - 1) - 1.0;
            t[static_cast<size_t>(i % 4)][static_cast<size_t>(i / 4)] = sinc * besselI0(beta * std::sqrt(1.0 - r * r)) / besselI0(beta);
        }
        for (auto& phase : t)
        {
            double sum = 0.0;
            for (const double v : phase)
                sum += v;
            for (double& v : phase)
                v /= sum;
        }
        return t;
    }();
    return taps;
}

// numpy.hanning(length + 2)[1:-1] normalised to sum 1 (the prototype's smoothing FIR)
void hann(std::vector<double>& w, size_t length)
{
    const double m = static_cast<double>(length + 2);
    w.resize(length);
    double sum = 0.0;
    for (size_t k = 0; k < length; ++k)
    {
        const double nk = 1.0 - m + 2.0 * static_cast<double>(k + 1);
        w[k] = 0.5 + 0.5 * std::cos(std::numbers::pi * nk / (m - 1.0));
        sum += w[k];
    }
    for (double& v : w)
        v /= sum;
}
} // namespace

void TruePeakInterpolator::reset()
{
    history.fill(0.0);
    position = 0;
}

std::array<double, 4> TruePeakInterpolator::process(double x)
{
    history[static_cast<size_t>(position)] = x;
    const auto& taps = interpolationTaps();
    std::array<double, 4> out{};
    for (size_t p = 0; p < 4; ++p)
    {
        double sum = 0.0;
        for (int j = 0; j < kTapsPerPhase; ++j) // x[n - j]
            sum += taps[p][static_cast<size_t>(j)] *
                   history[static_cast<size_t>((position - j + kTapsPerPhase) % kTapsPerPhase)];
        out[p] = sum;
    }
    position = (position + 1) % kTapsPerPhase;
    return out;
}

void Limiter::prepare(double sampleRate, int, Quality q)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    quality = q;
    delay = q == Quality::Mix ? static_cast<int>(std::nearbyint(0.002 * fs))
                              : (q == Quality::Print ? static_cast<int>(std::nearbyint(0.005 * fs)) : 0);
    dry.prepare(delay);
    dry.setDelay(delay);
    hold.prepare(delay + 1);
    fir.reserve(static_cast<size_t>(delay) + 1);
    released.reserve(static_cast<size_t>(delay) + 1);
    interpolationTaps();
    reset();
}

void Limiter::configure()
{
    lag = quality == Quality::Print && params.truePeak ? TruePeakInterpolator::kDelay : 0;
    const auto length = static_cast<size_t>(std::max(1, delay + 1 - lag));
    hold.setWindow(static_cast<int>(length));
    hann(fir, length);
    released.assign(length, 0.0); // the prototype's FIR starts from zeros (lfilter)
    releasedPos = 0;
}

void Limiter::reset()
{
    dry.reset();
    hold.reset(-1.0); // gain 1 before the start
    releaseState = 1.0;
    std::fill(released.begin(), released.end(), 0.0);
    releasedPos = 0;
    previousX = 0.0;
    interpolator.reset();
    recent.fill(0.0);
    recentPos = 0;
    lastSegment = 0.0;
    lastGain = 1.0;
    lastDelayed = 0.0;
    fresh = true;
}

void Limiter::setParams(const LimiterParams& p)
{
    const bool detectionChanged = fresh || p.truePeak != params.truePeak;
    params = p;
    params.ceilingDb = std::isfinite(p.ceilingDb) ? std::clamp(p.ceilingDb, -60.0, 0.0) : -1.0;
    params.releaseMs = std::isfinite(p.releaseMs) ? std::clamp(p.releaseMs, 1.0, 5000.0) : 100.0;
    releaseCoeff = 1.0 - std::exp(-1.0 / (params.releaseMs * 1e-3 * fs));
    if (detectionChanged)
    {
        configure();
        hold.reset(-1.0);
    }
    if (fresh)
    {
        fresh = false;
        ceilingDb.reset(fs, kSmoothMs, params.ceilingDb);
        wet.reset(fs, kSwitchMs, params.on ? 1.0 : 0.0);
        return;
    }
    ceilingDb.setTarget(params.ceilingDb);
    wet.setTarget(params.on ? 1.0 : 0.0);
}

double Limiter::gainReductionDb() const { return lastGain > 0.0 ? 20.0 * std::log10(lastGain) : -400.0; }

double Limiter::limitLive(double x, double c)
{
    // c tanh(x / c) with ADAA: F1 = c^2 ln cosh(x / c)
    const double du = x - previousX;
    const double y = std::abs(du) > kAdaaTolerance
                         ? c * c * (drive_detail::logCosh(x / c) - drive_detail::logCosh(previousX / c)) / du
                         : c * std::tanh(0.5 * (x + previousX) / c);
    previousX = x;
    lastGain = std::abs(x) > 1e-12 ? y / x : 1.0;
    return y;
}

double Limiter::limitLookahead(double x, double c)
{
    double level = std::abs(x);
    if (lag > 0) // true peak: the interval peaks next to sample n - 8
    {
        const auto phases = interpolator.process(x);
        recent[recentPos] = x;
        const double older = recent[(recentPos + 1) % recent.size()]; // x[n - 8]
        const double newer = recent[(recentPos + 2) % recent.size()]; // x[n - 7]
        recentPos = (recentPos + 1) % recent.size();
        double segment = std::max(std::abs(older), std::abs(newer));
        for (const double v : phases)
            segment = std::max(segment, std::abs(v));
        level = std::max(segment, lastSegment); // [n - 9, n - 8] and [n - 8, n - 7]
        lastSegment = segment;
    }
    const double required = level > c ? c / level : 1.0;
    const double held = -hold.process(-required);
    releaseState = held < releaseState ? held : releaseState + releaseCoeff * (held - releaseState);
    released[releasedPos] = releaseState;
    double g = 0.0;
    const size_t n = released.size();
    for (size_t k = 0; k < n; ++k) // FIR: tap k on the value k samples back
        g += fir[k] * released[(releasedPos + n - k) % n];
    releasedPos = (releasedPos + 1) % n;
    lastGain = g;
    lastDelayed = dry.process(x);
    return lastDelayed * g;
}

void Limiter::process(std::span<double> block)
{
    if (fresh)
        setParams(params);
    for (auto& x : block)
    {
        const double c = std::pow(10.0, ceilingDb.next() / 20.0);
        const double w = sCurve(wet.next());
        if (delay == 0)
        {
            const double limited = limitLive(x, c);
            x = w == 0.0 ? x : (w == 1.0 ? limited : w * limited + (1.0 - w) * x);
        }
        else
        {
            const double limited = limitLookahead(x, c); // also delays x: the bypass
            x = w == 1.0 ? limited : (w == 0.0 ? lastDelayed : w * limited + (1.0 - w) * lastDelayed);
        }
    }
}
} // namespace subgen::chain
