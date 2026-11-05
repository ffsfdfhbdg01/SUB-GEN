// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/Harmonic.h"

#include <algorithm>
#include <cmath>

#include "dsp/FilterDesign.h"

namespace subgen::engines
{
namespace
{
constexpr double kFloor = 1e-4; // E_FLOOR, -80 dBFS
constexpr double kAttackMs = 5.0;
constexpr double kReleaseMs = 60.0;
constexpr double kDcHz = 15.0;
} // namespace

void Harmonic::prepare(double sampleRate, int, int lookahead)
{
    fs = sampleRate;
    band.prepare(fs);
    hold.prepare(static_cast<int>(std::ceil(1.5 * fs / 15.0)) + 1);
    follower.prepare(fs, kAttackMs, kReleaseMs);
    dcBlock.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::HighPass, 2, kDcHz, fs));
    delay.prepare(std::max(0, lookahead));
    delay.setDelay(std::max(0, lookahead));
    mpm.prepare(fs, 300.0);
    frames.assign(static_cast<size_t>(mpm.maxFrames(1)), PitchFrame{});
    setControl(control);
    reset();
}

void Harmonic::reset()
{
    band.reset();
    hold.reset();
    follower.reset();
    dcBlock.reset();
    delay.reset();
    mpm.reset();
    prior.reset();
    heldF0 = 0.0;
}

void Harmonic::setControl(const EngineControl& requested)
{
    const EngineControl c = clampControl(requested);
    control = c;
    double fLo = c.rangeLoHz;
    if (c.baseFixed)
    {
        band.setBandpass(c.baseFreqHz);
        fLo = c.baseFreqHz * std::pow(2.0, -0.5); // the band's lower edge
    }
    else
        band.setRange(c.rangeLoHz, c.rangeHiHz, c.rangeSlope);
    hold.setWindow(std::max(static_cast<int>(std::ceil(1.5 * fs / fLo)), 1));
    mpm.setMinFrequency(c.rangeLoHz);
    harmMax = c.harmMax;
    weights.fill(0.0);
    for (int k = 2; k <= harmMax; ++k)
    {
        const double ck = std::max(k % 2 == 0 ? 1.0 - c.harmBalance : 1.0 + c.harmBalance, 0.0);
        weights[static_cast<size_t>(k)] = c.harmAmount * ck / k;
    }
}

double Harmonic::subFrequencyHz() const
{
    if (control.baseFixed)
        return control.baseFreqHz;
    return heldF0;
}

void Harmonic::process(std::span<const double> source, std::span<const double>, std::span<double> out)
{
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = processSample(i < source.size() ? source[i] : 0.0);
}

double Harmonic::processSample(double source)
{
    const double xb = band.process(source);
    if (!control.baseFixed && mpm.process(std::span<const double>(&xb, 1), frames) > 0)
    {
        const double f0 = prior.update(frames[0].f0Hz, frames[0].confidence);
        heldF0 = std::isfinite(f0) ? f0 : 0.0;
    }
    const double e = follower.process(hold.process(std::abs(xb)));
    const double xn = std::clamp(xb / std::max(e, kFloor), -1.0, 1.0);
    double previous = 1.0; // T_0
    double t = xn;         // T_1
    double sum = 0.0;
    for (int k = 2; k <= harmMax; ++k)
    {
        const double next = 2.0 * xn * t - previous;
        previous = t;
        t = next;
        sum += weights[static_cast<size_t>(k)] * t;
    }
    return control.sub1Gain * delay.process(dcBlock.process(e * sum));
}
} // namespace subgen::engines
