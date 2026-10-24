// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/EngineHost.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::engines
{
namespace
{
constexpr double kWarmUpSeconds = 0.2;
constexpr double kCrossfadeMs = 30.0;
} // namespace

void EngineHost::prepare(double sampleRate, int block, Quality quality)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0; // a broken rate must not size the rings

    maxBlock = std::max(1, block);
    lookahead = engineLookaheadSamples(quality, fs);
    for (const EngineType t : {EngineType::Divide, EngineType::Track, EngineType::Trigger, EngineType::Harmonic,
                               EngineType::Hybrid})
    {
        Engine& e = engineFor(t);
        e.prepare(fs, maxBlock, lookahead);
        e.setControl(control);
    }
    scratch.assign(static_cast<size_t>(maxBlock), 0.0);
    const auto ring = std::max<size_t>(1, static_cast<size_t>(std::ceil(kWarmUpSeconds * fs)));
    sourceRing.assign(ring, 0.0);
    keyRing.assign(ring, 0.0);
    ringPosition = 0;
    ringFill = 0;
    fadeLength = std::max(1, msToSamples(kCrossfadeMs, fs));
    fadePosition = 0;
    switching = false;
    active = target;
}

void EngineHost::setControl(const EngineControl& c)
{
    control = c;
    engineFor(active).setControl(c);
    if (switching)
        engineFor(incoming).setControl(c);
}

Engine& EngineHost::engineFor(EngineType type)
{
    switch (type)
    {
    case EngineType::Track:
        return track;
    case EngineType::Trigger:
        return trigger;
    case EngineType::Harmonic:
        return harmonic;
    case EngineType::Hybrid:
        return hybrid;
    case EngineType::Divide:
        break;
    }
    return divide;
}

const Engine& EngineHost::reported() const
{
    return const_cast<EngineHost*>(this)->engineFor(switching ? incoming : active);
}

// The incoming engine starts from a clean state and runs over the buffered source, so its band, envelopes, pitch
// track and lookahead are full when it is heard.
void EngineHost::beginSwitch()
{
    incoming = target;
    Engine& e = engineFor(incoming);
    e.reset();
    e.setControl(control);
    size_t start = (ringPosition + sourceRing.size() - ringFill) % sourceRing.size();
    size_t remaining = ringFill;
    while (remaining > 0)
    {
        const size_t len = std::min({remaining, scratch.size(), sourceRing.size() - start});
        e.process(std::span<const double>(sourceRing.data() + start, len),
                  std::span<const double>(keyRing.data() + start, len), std::span<double>(scratch.data(), len));
        start = (start + len) % sourceRing.size();
        remaining -= len;
    }
    switching = true;
    fadePosition = 0;
}

void EngineHost::process(std::span<const double> source, std::span<const double> key, std::span<double> out)
{
    if (scratch.empty() || sourceRing.empty()) // not prepared: silence
    {
        std::fill(out.begin(), out.end(), 0.0);
        return;
    }
    for (size_t pos = 0; pos < out.size(); pos += scratch.size())
    {
        const size_t len = std::min(scratch.size(), out.size() - pos);
        const auto src = pos < source.size() ? source.subspan(pos, std::min(len, source.size() - pos))
                                             : std::span<const double>{};
        const auto k = pos < key.size() ? key.subspan(pos, std::min(len, key.size() - pos)) : std::span<const double>{};
        processChunk(src, k, out.subspan(pos, len));
    }
}

void EngineHost::processChunk(std::span<const double> source, std::span<const double> key, std::span<double> out)
{
    if (!switching && target != active)
        beginSwitch();
    engineFor(active).process(source, key, out);
    if (switching)
    {
        const auto in = std::span<double>(scratch.data(), out.size());
        engineFor(incoming).process(source, key, in);
        for (size_t i = 0; i < out.size(); ++i)
        {
            const double u = static_cast<double>(fadePosition + static_cast<int>(i) + 1) / fadeLength;
            if (u >= 1.0)
                out[i] = in[i];
            else
                out[i] = std::cos(0.5 * std::numbers::pi * u) * out[i] + std::sin(0.5 * std::numbers::pi * u) * in[i];
        }
        fadePosition += static_cast<int>(out.size());
        if (fadePosition >= fadeLength)
        {
            active = incoming;
            switching = false;
        }
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        sourceRing[ringPosition] = i < source.size() ? source[i] : 0.0;
        keyRing[ringPosition] = i < key.size() ? key[i] : 0.0;
        ringPosition = (ringPosition + 1) % sourceRing.size();
    }
    ringFill = std::min(sourceRing.size(), ringFill + out.size());
}
} // namespace subgen::engines
