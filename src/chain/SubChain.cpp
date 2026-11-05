// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/SubChain.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::chain
{
namespace
{
constexpr double kLevelMs = 20.0;
constexpr double kFadeMs = 20.0; // drive_pos: X 20 ms
constexpr double kWarmMs = 50.0;
constexpr double kMaxLevel = 7.943282347242815; // +18 dB

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }
} // namespace

void SubChain::Dynamics::process(std::span<double> block)
{
    if (postComp)
    {
        comp.process(block);
        drive.process(block);
    }
    else
    {
        drive.process(block);
        comp.process(block);
    }
}

void SubChain::prepare(double sampleRate, int maxBlockSize, Quality quality)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = static_cast<size_t>(std::max(maxBlockSize, 1));
    const int block = static_cast<int>(maxBlock);
    boost.prepare(fs);
    gate.prepare(fs, block);
    for (auto& d : dynamics)
    {
        d.drive.prepare(fs, block, quality);
        d.comp.prepare(fs, block, quality);
    }
    eq.prepare(fs);
    filters.prepare(fs, block, quality);
    phase.prepare(fs);
    limiter.prepare(fs, block, quality);
    history.assign(static_cast<size_t>(std::max(1, msToSamples(kWarmMs, fs))), 0.0);
    scratch.assign(maxBlock, 0.0);
    chainA.assign(maxBlock, 0.0);
    reset();
}

void SubChain::reset()
{
    boost.reset();
    gate.reset();
    for (auto& d : dynamics)
    {
        d.drive.reset();
        d.comp.reset();
    }
    eq.reset();
    filters.reset();
    phase.reset();
    limiter.reset();
    std::fill(history.begin(), history.end(), 0.0);
    written = 0;
    fading = false;
    lastBlock = 0;
    fresh = true;
}

void SubChain::setParams(const SubChainParams& p)
{
    params = p;
    applyParams();
}

void SubChain::applyParams()
{
    const double g = std::isfinite(params.levelGain) ? std::clamp(params.levelGain, 0.0, kMaxLevel) : 0.0;
    if (fresh)
    {
        fresh = false;
        level.reset(fs, kLevelMs, g);
        dynamics[active].postComp = params.drivePostComp;
    }
    else
        level.setTarget(g);
    boost.setParams(params.boost);
    gate.setParams(params.gate);
    for (size_t i = 0; i < dynamics.size(); ++i)
        if (i == active || fading) // the idle pair gets its parameters when it is switched in
        {
            dynamics[i].drive.setParams(params.drive);
            dynamics[i].comp.setParams(params.comp);
        }
    eq.setParams(params.eq);
    filters.setParams(params.filters);
    phase.setParams(params.phase);
    limiter.setParams(params.limiter);
}

void SubChain::startDynamicsSwitch()
{
    const size_t incoming = active ^ 1;
    Dynamics& d = dynamics[incoming];
    d.postComp = params.drivePostComp;
    d.drive.reset();
    d.comp.reset();
    d.drive.setParams(params.drive);
    d.comp.setParams(params.comp);
    for (size_t i = 0; i < history.size(); i += maxBlock) // warm-up, the oldest sample first
    {
        const size_t n = std::min(maxBlock, history.size() - i);
        for (size_t j = 0; j < n; ++j)
            scratch[j] = history[(written + i + j) % history.size()];
        d.process(std::span<double>(scratch).subspan(0, n));
    }
    active = incoming;
    fade.reset(fs, kFadeMs, 0.0);
    fade.setTarget(1.0);
    fading = true;
}

void SubChain::process(std::span<double> block)
{
    nanCaught = false;
    if (fresh)
    {
        std::fill(block.begin(), block.end(), 0.0);
        lastBlock = 0;
        return;
    }
    for (size_t done = 0; done < block.size();)
    {
        const size_t n = std::min(block.size() - done, maxBlock);
        processChunk(block.subspan(done, n));
        done += n;
    }
}

void SubChain::processChunk(std::span<double> x)
{
    for (auto& v : x)
        v *= level.next();
    boost.process(x);
    gate.process(x);
    if (!fading && params.drivePostComp != dynamics[active].postComp)
        startDynamicsSwitch();
    for (const double v : x)
    {
        history[written] = v;
        written = written + 1 == history.size() ? 0 : written + 1;
    }
    if (fading)
    {
        const auto outgoing = std::span<double>(scratch).subspan(0, x.size());
        std::copy(x.begin(), x.end(), outgoing.begin());
        dynamics[active ^ 1].process(outgoing);
        dynamics[active].process(x);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double w = sCurve(fade.next());
            x[i] = w * x[i] + (1.0 - w) * outgoing[i];
        }
        fading = fade.isSmoothing();
    }
    else
        dynamics[active].process(x);
    eq.process(x);
    filters.process(x);
    phase.process(x, std::span<double>(chainA).subspan(0, x.size()));
    lastBlock = x.size();
    limiter.process(x);
    if (!std::all_of(x.begin(), x.end(), [](double v) { return std::isfinite(v); }))
    {
        std::fill(x.begin(), x.end(), 0.0); // the guard (T-RT-03): silence, fresh states, the same parameters
        std::fill(chainA.begin(), chainA.end(), 0.0);
        reset();
        applyParams();
        lastBlock = x.size();
        nanCaught = true;
    }
}

int SubChain::latencySamples() const
{
    const Dynamics& d = dynamics[active];
    return d.drive.latencySamples() + d.comp.latencySamples() + filters.latencySamples() + limiter.latencySamples();
}
} // namespace subgen::chain
