// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/Drive.h"

#include <algorithm>
#include <cmath>

#include <juce_dsp/juce_dsp.h>

#include "chain/AutoGainTable.h"
#include "dsp/FilterDesign.h"

namespace subgen::chain
{
namespace
{
const double kRef = std::pow(10.0, 12.0 / 20.0); // -12 dBFS peak is +-1 in the shaper domain
constexpr double kAdaaTolerance = 1e-5;
constexpr double kFoldCap = 2.0; // x_ref of a -6 dBFS peak
constexpr double kFoldReleaseMs = 50.0;
constexpr double kSmoothMs = 20.0;
constexpr int kGainBlock = 64; // Auto Gain target updates
} // namespace

Drive::Drive() = default;
Drive::~Drive() = default;

void Drive::prepare(double sampleRate, int block, Quality quality)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = std::max(1, block);
    using Os = juce::dsp::Oversampling<double>;
    mixOversampler = std::make_unique<Os>(1, 1, Os::filterHalfBandPolyphaseIIR, true, true);
    printOversampler = std::make_unique<Os>(1, 2, Os::filterHalfBandFIREquiripple, true, true);
    mixOversampler->initProcessing(static_cast<size_t>(maxBlock));
    printOversampler->initProcessing(static_cast<size_t>(maxBlock));
    oversampler = quality == Quality::Mix ? mixOversampler.get()
                                          : (quality == Quality::Print ? printOversampler.get() : nullptr);
    factor = quality == Quality::Mix ? 2 : (quality == Quality::Print ? 4 : 1);
    latency = oversampler != nullptr ? static_cast<int>(std::ceil(oversampler->getLatencyInSamples())) : 0;
    dry.prepare(latency);
    dry.setDelay(latency);
    dcBlock.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::HighPass, 1, 5.0, fs));
    foldHold.prepare(static_cast<int>(std::ceil(0.075 * fs)) + 1); // 1.5 periods of 20 Hz
    foldRelease = std::exp(-1.0 / (kFoldReleaseMs * 1e-3 * fs));
    const auto size = static_cast<size_t>(maxBlock);
    for (auto* v : {&u, &xDelayed, &alpha, &biasCurrent, &biasFading, &fadeWeight, &scale, &toneHz, &gains, &wets})
        v->assign(size, 0.0);
    AutoGainTable::instance(); // computed once per process
    reset();
}

void Drive::reset()
{
    if (mixOversampler)
        mixOversampler->reset();
    if (printOversampler)
        printOversampler->reset();
    dry.reset();
    toneLp.reset();
    dcBlock.reset();
    foldHold.reset();
    foldEnvelope = 0.0;
    previousCurrent = previousFading = 0.0;
    active = false;
    modeFading = false;
    fresh = true;
    lastToneHz = 0.0;
}

void Drive::setParams(const DriveParams& p)
{
    params = p;
    params.drivePct = std::isfinite(p.drivePct) ? std::clamp(p.drivePct, 0.0, 100.0) : 0.0;
    params.balance = std::isfinite(p.balance) ? std::clamp(p.balance, -100.0, 100.0) : 0.0;
    params.toneHz = std::isfinite(p.toneHz) ? std::clamp(p.toneHz, 60.0, 2000.0) : 500.0;
    params.fSub1Hz = std::isfinite(p.fSub1Hz) ? std::clamp(p.fSub1Hz, 20.0, 500.0) : 40.0;
    foldHold.setWindow(std::max(1, static_cast<int>(std::lround(std::max(0.010, 1.5 / params.fSub1Hz) * fs))));
    if (fresh)
    {
        fresh = false;
        drive.reset(fs, kSmoothMs, params.drivePct);
        balance.reset(fs, kSmoothMs, params.balance);
        tone.reset(fs, kSmoothMs, params.toneHz);
        gain.reset(fs, kSmoothMs, 1.0);
        wet.reset(fs, kSmoothMs, params.drivePct > 0.0 ? 1.0 : 0.0);
        fade.reset(fs, kSmoothMs, 1.0);
        current = previous = params.mode;
        active = params.drivePct > 0.0;
        return;
    }
    drive.setTarget(params.drivePct);
    balance.setTarget(params.balance);
    tone.setTarget(params.toneHz);
    if (!active)
    {
        current = previous = params.mode; // nothing sounds: no crossfade
        if (params.drivePct > 0.0)
            activate();
        return;
    }
    wet.setTarget(params.drivePct > 0.0 ? 1.0 : 0.0);
    if (!modeFading && params.mode != current)
    {
        previous = current;
        current = params.mode;
        previousFading = previousCurrent; // both paths start from the same input
        fade.reset(fs, kSmoothMs, 0.0);
        fade.setTarget(1.0);
        modeFading = true;
    }
}

// From the bypass: the shaping path starts clean, drive ramps up from 0 and the output crossfades in.
void Drive::activate()
{
    if (oversampler != nullptr)
        oversampler->reset();
    toneLp.reset();
    dcBlock.reset();
    foldHold.reset();
    foldEnvelope = 0.0;
    previousCurrent = previousFading = 0.0;
    drive.reset(fs, kSmoothMs, 0.0);
    drive.setTarget(params.drivePct);
    gain.reset(fs, kSmoothMs, 1.0);
    wet.reset(fs, kSmoothMs, 0.0);
    wet.setTarget(1.0);
    fade.reset(fs, kSmoothMs, 1.0);
    modeFading = false;
    lastToneHz = 0.0;
    active = true;
}

double Drive::shapeSample(DriveMode mode, double x, double bias, double& prev) const
{
    double y = 0.0;
    if (!params.adaa)
        y = shape(mode, x, bias);
    else
    {
        const double du = x - prev;
        y = std::abs(du) > kAdaaTolerance ? (antiderivative(mode, x, bias) - antiderivative(mode, prev, bias)) / du
                                          : shape(mode, 0.5 * (x + prev), bias);
    }
    prev = x;
    return y;
}

// min(1, 2 / env): env holds |x_ref| over the window, releases over 50 ms, rises at once
double Drive::foldScale(double xRef)
{
    const double held = foldHold.process(std::abs(xRef));
    foldEnvelope = held >= foldEnvelope ? held : std::max(held, foldEnvelope * foldRelease);
    return std::min(1.0, kFoldCap / std::max(foldEnvelope, 1e-300));
}

void Drive::process(std::span<double> block)
{
    if (u.empty()) // not prepared
        return;
    for (size_t pos = 0; pos < block.size(); pos += static_cast<size_t>(maxBlock))
        processChunk(block.subspan(pos, std::min(static_cast<size_t>(maxBlock), block.size() - pos)));
}

void Drive::processChunk(std::span<double> x)
{
    const size_t n = x.size();
    if (!active)
    {
        for (auto& v : x)
            v = dry.process(v);
        return;
    }
    // base rate: smoothed parameters, the shaper input and the dry path
    for (size_t i = 0; i < n; ++i)
    {
        if (i % kGainBlock == 0) // Auto Gain target from the parameters as they are now
        {
            const double d = drive.current();
            const double db = params.autoGain && d > 0.0 // the shaper only (F-8): the Tone is not compensated
                                  ? AutoGainTable::instance().compensationDb(current, d, balance.current(), params.fSub1Hz, fs)
                                  : 0.0;
            gain.setTarget(std::pow(10.0, db / 20.0));
        }
        const double d = drive.next();
        const double b = balance.next();
        const double xRef = x[i] * kRef;
        alpha[i] = std::min(1.0, d / 5.0);
        u[i] = std::pow(10.0, (30.0 * d / 100.0) / 20.0) * xRef;
        scale[i] = params.foldCap ? foldScale(xRef) : 1.0;
        biasCurrent[i] = effectiveBias(current, b);
        biasFading[i] = effectiveBias(previous, b);
        fadeWeight[i] = modeFading ? fade.next() : 1.0;
        toneHz[i] = tone.next();
        gains[i] = gain.next();
        wets[i] = wet.next();
        xDelayed[i] = dry.process(x[i]);
    }

    // the shapers, oversampled
    auto shapeAt = [&](double* data, size_t count, size_t ratio) {
        for (size_t j = 0; j < count; ++j)
        {
            const size_t i = j / ratio;
            const double v = data[j];
            double y = shapeSample(current, current == DriveMode::Fold ? v * scale[i] : v, biasCurrent[i], previousCurrent);
            if (modeFading)
            {
                const double old =
                    shapeSample(previous, previous == DriveMode::Fold ? v * scale[i] : v, biasFading[i], previousFading);
                y = fadeWeight[i] * y + (1.0 - fadeWeight[i]) * old;
            }
            data[j] = y;
        }
    };
    if (oversampler == nullptr)
        shapeAt(u.data(), n, 1);
    else
    {
        double* channel = u.data();
        juce::dsp::AudioBlock<double> base(&channel, 1, n);
        auto up = oversampler->processSamplesUp(base);
        shapeAt(up.getChannelPointer(0), up.getNumSamples(), static_cast<size_t>(factor));
        oversampler->processSamplesDown(base);
    }

    // blend, Tone, DC block, Auto Gain, bypass crossfade
    for (size_t i = 0; i < n; ++i)
    {
        double y = (1.0 - alpha[i]) * xDelayed[i] + alpha[i] * u[i] / kRef;
        if (params.tone)
        {
            if (toneHz[i] != lastToneHz)
            {
                toneLp.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::LowPass, 2, toneHz[i], fs));
                lastToneHz = toneHz[i];
            }
            y += alpha[i] * (toneLp.process(y) - y);
        }
        const double shaped = dcBlock.process(y) * gains[i];
        x[i] = wets[i] * shaped + (1.0 - wets[i]) * xDelayed[i];
    }
    if (modeFading && !fade.isSmoothing())
        modeFading = false;
    if (wet.current() == 0.0 && !wet.isSmoothing() && params.drivePct == 0.0)
        active = false; // the bypass carries on from here
}
} // namespace subgen::chain
