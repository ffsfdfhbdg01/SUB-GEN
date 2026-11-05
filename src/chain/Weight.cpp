// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/Weight.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::chain
{
namespace
{
constexpr double kQ = 1.0;
constexpr double kDrive = 4.0;      // the band's level into the saturator
constexpr double kBias = 0.25;      // the saturator's asymmetry: even harmonics
constexpr double kHarmonics = 0.6;  // the harmonics' share of the lift
const double kSlope = 1.0 / std::pow(1.0 / std::cosh(kBias), 2.0); // 1 / tanh'(bias): unity small-signal gain

// the saturator's nonlinear part: tanh(k b + c) - tanh(c), scaled to unity slope, less b
double harmonicsOf(double b)
{
    return (std::tanh(kDrive * b + kBias) - std::tanh(kBias)) * kSlope / kDrive - b;
}
} // namespace

void Weight::prepare(double sampleRate)
{
    fs = sampleRate;
    lift.reset(fs, 20.0, 0.0);
    freq.reset(fs, 20.0, 80.0);
    dcBlock.set({std::tan(std::numbers::pi * 10.0 / fs), true});
    madeFreq = -1.0;
    updateFilter(80.0);
    reset();
}

void Weight::reset()
{
    band1.reset();
    band2.reset();
    dcBlock.reset();
}

void Weight::setParams(double amountDb, double freqHz, bool immediate)
{
    const double g = std::pow(10.0, std::clamp(amountDb, 0.0, 24.0) / 20.0);
    const double l = amountDb <= 0.0 ? 0.0 : g - 1.0;
    const double f = std::clamp(freqHz, 20.0, 0.45 * fs);
    if (immediate)
    {
        lift.reset(fs, 20.0, l);
        freq.reset(fs, 20.0, f);
        return;
    }
    lift.setTarget(l);
    freq.setTarget(f);
}

void Weight::updateFilter(double freqHz)
{
    if (freqHz == madeFreq)
        return;
    const auto c = dsp::makeSvf(dsp::SvfMode::BandPass, freqHz, kQ, 0.0, fs);
    band1.set(c);
    band2.set(c);
    madeFreq = freqHz;
}

void Weight::process(std::span<double> block)
{
    for (double& x : block)
    {
        updateFilter(freq.next());
        const double l = lift.next();
        const double b = band2.process(band1.process(x));
        const double h = dcBlock.process(harmonicsOf(b));
        if (l != 0.0)
            x += l * (b + kHarmonics * h);
    }
}
} // namespace subgen::chain
