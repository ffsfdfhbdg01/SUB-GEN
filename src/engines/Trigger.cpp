// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/Trigger.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/Quality.h"

namespace subgen::engines
{
namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kVelocityRef = 0.5;
constexpr double kVelocityMax = 1.5;
constexpr double kMinAttackMs = 0.5; // D8 M5: a 0 ms attack off 0/180 degrees would step by the amplitude
constexpr double kSilent = 1e-10;    // a decayed voice below this stops (the prototype renders it on)
const double kFollowTolerance = std::pow(10.0, 1.0 / 20.0);

double wrapTwoPi(double x) { return x - kTwoPi * std::floor(x / kTwoPi); }
} // namespace

void Trigger::prepare(double sampleRate, int, int lookahead)
{
    fs = sampleRate;
    look = std::max(0, lookahead);
    band.prepare(fs);
    detector.prepare(fs);
    rms.prepare(fs, 20.0);
    mpm.prepare(fs, 300.0);
    frames.assign(static_cast<size_t>(mpm.maxFrames(1)), PitchFrame{});
    rmsRing.assign(static_cast<size_t>(look) + 2, 0.0);
    peakWindow = msToSamples(20.0, fs);
    fadeSamples = std::max(msToSamples(3.0, fs), 1);
    setControl(control);
    reset();
}

void Trigger::reset()
{
    band.reset();
    detector.reset();
    rms.reset();
    mpm.reset();
    prior.reset();
    std::fill(rmsRing.begin(), rmsRing.end(), 0.0);
    pendingCount = 0;
    current = Voice{};
    fading = Voice{};
    n = 0;
    latestF0 = 0.0;
    lastFrequency = 0.0;
    lastProbe = TriggerProbe{};
}

void Trigger::setControl(const EngineControl& requested)
{
    const EngineControl c = clampControl(requested);
    control = c;
    band.setRange(c.rangeLoHz, c.rangeHiHz, c.rangeSlope);
    detector.set(c.trigThreshDb, c.trigHoldMs, c.trigLockoutMs);
    mpm.setMinFrequency(c.rangeLoHz);
    attack = std::max(c.trigAttackMs, kMinAttackMs) * 1e-3;
    t60 = c.trigDecayMs * 1e-3;
    tau = c.trigPitchTimeMs * 1e-3;
    phase0 = c.trigPhaseDeg * (std::numbers::pi / 180.0);
}

void Trigger::process(std::span<const double> source, std::span<const double> key, std::span<double> out)
{
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = processSample(i < source.size() ? source[i] : 0.0, i < key.size() ? key[i] : 0.0);
}

double Trigger::velocityOf(double peak) const
{
    if (control.trigVel <= 0.0)
        return 1.0;
    return std::clamp(std::pow(peak / kVelocityRef, control.trigVel), 0.0, kVelocityMax);
}

// 2 pi * integral of f over [0, s], f(t) = f_end + (f_start - f_end) exp(-t / tau_p)
double Trigger::phaseAdvance(const Voice& v, double s) const
{
    return kTwoPi * (v.fEnd * s + (control.trigFStartHz - v.fEnd) * tau * (1.0 - std::exp(-s / tau)));
}

// attack from v.from to the velocity over t_a (linear), then the T60 decay
double Trigger::level(const Voice& v, double s) const
{
    if (s < attack)
        return v.from + (v.velocity - v.from) * (s / attack);
    return v.velocity * std::pow(10.0, -3.0 * (s - attack) / t60);
}

void Trigger::startVoice(std::int64_t m, const Pending& p)
{
    Voice next;
    next.started = next.sounding = true;
    next.onset = p.time;
    next.peak = p.peak;
    next.remaining = p.remaining;
    next.velocity = velocityOf(p.peak);
    // Auto: the prior's held pitch (its last track, kept through the unvoiced frames a transient causes)
    next.fEnd = !control.baseFixed && latestF0 > 0.0 ? std::clamp(0.5 * latestF0, 20.0, 100.0) : control.baseFreqHz;
    if (control.trigLegato && current.started)
    {
        // phase and level of the running voice at the new onset
        const double sEnd = (p.time - current.onset) / fs;
        const double inc = phaseAdvance(current, sEnd);
        next.phi1 = wrapTwoPi(current.phi1 + inc);
        next.phi2 = wrapTwoPi(current.phi2 + 0.5 * inc);
        next.from = level(current, sEnd) * (control.follow ? current.gain : 1.0);
    }
    else
    {
        next.phi1 = next.phi2 = phase0;
        if (current.sounding)
        {
            fading = current;
            fading.fading = true;
            fading.fadeFrom = p.time;
        }
        // the prototype's segment starts at floor(onset): Follow's running maximum includes that sample
        if (std::floor(p.time) < static_cast<double>(m))
            next.runMax = rmsRing[static_cast<size_t>(m - 1) % rmsRing.size()];
    }
    current = next;
}

void Trigger::render(Voice& v, std::int64_t m, double e, double& y1, double& y2)
{
    const double s = (static_cast<double>(m) - v.onset) / fs;
    double gain = 1.0;
    if (control.follow) // e_rms over its running maximum since the onset (1 dB tolerance), never rising again
    {
        v.runMax = std::max(v.runMax, e);
        const double r = (v.runMax > 0.0 ? e / v.runMax : 1.0) * kFollowTolerance;
        v.gain = std::min(v.gain, std::min(r, 1.0));
        gain = v.gain;
    }
    double weight = 1.0;
    if (v.fading)
    {
        const double u = std::clamp((static_cast<double>(m) - v.fadeFrom) / fadeSamples, 0.0, 1.0);
        const double c = std::cos(0.5 * std::numbers::pi * u);
        weight = c * c;
        if (u >= 1.0)
            v.sounding = false;
    }
    const double a = level(v, s);
    const double inc = phaseAdvance(v, s);
    y1 = a * gain * weight * std::sin(v.phi1 + inc);
    y2 = a * gain * weight * std::sin(v.phi2 + 0.5 * inc);
    if (s >= attack && v.remaining == 0 && a < kSilent)
        v.sounding = false;
}

double Trigger::processSample(double source, double key)
{
    const std::int64_t cur = n++;
    const double xb = band.process(source);

    // current axis: onsets, their velocity windows, e_rms and (Auto) the pitch
    if (const auto offset = detector.process(control.keyMode == KeyMode::Trigger ? key : xb))
        if (pendingCount < static_cast<int>(pending.size()))
            pending[static_cast<size_t>(pendingCount++)] = {static_cast<double>(cur) + *offset, 0.0, peakWindow};
    const double ax = std::abs(xb);
    for (int i = 0; i < pendingCount; ++i)
    {
        auto& p = pending[static_cast<size_t>(i)];
        if (p.remaining > 0)
        {
            p.peak = std::max(p.peak, ax);
            --p.remaining;
        }
    }
    for (Voice* v : {&current, &fading}) // Live: the window is still open when the voice starts
        if (v->started && v->remaining > 0)
        {
            v->peak = std::max(v->peak, ax);
            --v->remaining;
            v->velocity = velocityOf(v->peak);
        }
    rmsRing[static_cast<size_t>(cur) % rmsRing.size()] = rms.process(xb);
    if (!control.baseFixed && mpm.process(std::span<const double>(&xb, 1), frames) > 0)
    {
        const double held = prior.update(frames[0].f0Hz, frames[0].confidence);
        latestF0 = std::isfinite(held) ? held : 0.0;
    }

    // delayed axis: a voice starts at the first sample at or after its onset
    const std::int64_t m = cur - look;
    TriggerProbe p;
    if (m < 0)
    {
        lastProbe = p;
        return 0.0;
    }
    if (pendingCount > 0 && pending[0].time <= static_cast<double>(m))
    {
        const Pending next = pending[0];
        std::move(pending.begin() + 1, pending.begin() + pendingCount, pending.begin());
        --pendingCount;
        startVoice(m, next);
        p.onset = true;
        p.onsetTime = next.time;
        p.fEnd = current.fEnd;
        p.velocity = current.velocity;
    }
    const double e = rmsRing[static_cast<size_t>(m) % rmsRing.size()];
    double y1 = 0.0;
    double y2 = 0.0;
    for (Voice* v : {&fading, &current}) // the older voice first, as the prototype adds them
        if (v->sounding)
        {
            double a = 0.0;
            double b = 0.0;
            render(*v, m, e, a, b);
            y1 += a;
            y2 += b;
            ++p.voices;
        }
    const double s = (static_cast<double>(m) - current.onset) / fs;
    lastFrequency =
        current.sounding ? current.fEnd + (control.trigFStartHz - current.fEnd) * std::exp(-s / tau) : 0.0;
    lastProbe = p;
    return control.sub1Gain * y1 + control.sub2Gain * y2;
}
} // namespace subgen::engines
