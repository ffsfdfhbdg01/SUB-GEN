// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The plug-in processor driven as a host drives it: buses, parameters, rendering in blocks (plug-in tests only).

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "plugin/PluginProcessor.h"

namespace subgen::test
{
using Channels = std::vector<std::vector<double>>;

inline void setParam(plugin::SubGenProcessor& p, const char* id, float plain)
{
    auto* param = p.parameters().getParameter(id);
    jassert(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

inline float getParam(const plugin::SubGenProcessor& p, const char* id)
{
    return p.parameters().getRawParameterValue(id)->load();
}

// Weight off (on by default since 0.2): the dry stays bit-exact, for tests of routing, latency and the sum
inline void noWeight(plugin::SubGenProcessor& p) { setParam(p, "weight_amount", 0.0f); }

struct Layout
{
    juce::AudioChannelSet in = juce::AudioChannelSet::stereo();
    juce::AudioChannelSet out = juce::AudioChannelSet::stereo();
    juce::AudioChannelSet sidechain = juce::AudioChannelSet::disabled();
};

inline juce::AudioProcessor::BusesLayout busesOf(const Layout& l)
{
    juce::AudioProcessor::BusesLayout b;
    b.inputBuses.add(l.in);
    b.inputBuses.add(l.sidechain);
    b.outputBuses.add(l.out);
    return b;
}

// Buses set, parameters set by `setup`, then prepared; nullptr when the layout is refused.
template <typename Processor = plugin::SubGenProcessor>
std::unique_ptr<Processor> makeProcessor(const Layout& layout, double fs = 48000.0, int maxBlock = 512,
                                         const std::function<void(plugin::SubGenProcessor&)>& setup = {})
{
    auto p = std::make_unique<Processor>();
    if (!p->setBusesLayout(busesOf(layout)))
        return nullptr;
    if (setup)
        setup(*p);
    p->setRateAndBufferSizeDetails(fs, maxBlock);
    p->prepareToPlay(fs, maxBlock);
    return p;
}

// Renders the main input (and the sidechain's channels) in blocks of the given sizes, cycling through them; the
// buffer is float or double as a host's would be. Returns the main output channels.
template <typename Sample = double>
Channels render(plugin::SubGenProcessor& p, const Channels& main, const Channels& key, std::span<const size_t> blocks,
                const std::function<void(size_t)>& beforeBlock = {})
{
    const size_t n = main.empty() ? 0 : main.front().size();
    const int inCh = p.getMainBusNumInputChannels();
    const auto* sc = p.getBus(true, 1);
    const int scCh = sc != nullptr && sc->isEnabled() ? sc->getNumberOfChannels() : 0;
    const int outCh = p.getMainBusNumOutputChannels();
    const int total = std::max(inCh + scCh, outCh);
    Channels out(static_cast<size_t>(outCh), std::vector<double>(n, 0.0));
    juce::MidiBuffer midi;
    size_t pos = 0, k = 0;
    while (pos < n)
    {
        const size_t len = std::min(blocks[k++ % blocks.size()], n - pos);
        if (beforeBlock)
            beforeBlock(pos);
        juce::AudioBuffer<Sample> buffer(total, static_cast<int>(len));
        buffer.clear();
        for (int c = 0; c < inCh && static_cast<size_t>(c) < main.size(); ++c)
            for (size_t i = 0; i < len; ++i)
                buffer.setSample(c, static_cast<int>(i), static_cast<Sample>(main[static_cast<size_t>(c)][pos + i]));
        for (int c = 0; c < scCh && static_cast<size_t>(c) < key.size(); ++c)
            for (size_t i = 0; i < len; ++i)
                buffer.setSample(inCh + c, static_cast<int>(i), static_cast<Sample>(key[static_cast<size_t>(c)][pos + i]));
        p.processBlock(buffer, midi);
        for (int c = 0; c < outCh; ++c)
            for (size_t i = 0; i < len; ++i)
                out[static_cast<size_t>(c)][pos + i] = static_cast<double>(buffer.getSample(c, static_cast<int>(i)));
        pos += len;
    }
    return out;
}

template <typename Sample = double>
Channels render(plugin::SubGenProcessor& p, const Channels& main, size_t block = 512)
{
    const size_t blocks[] = {block};
    return render<Sample>(p, main, {}, blocks);
}

// SIG-15 (TEST_PLAN): channel k carries its own 1 kHz + 100 k Hz tone at -20 dBFS plus a 60 Hz bed at a level of its
// class, so routing and feeds can be told apart; float-exact values (the float tests compare bit for bit).
inline Channels sig15(const juce::AudioChannelSet& set, double fs, double seconds)
{
    const auto n = static_cast<size_t>(seconds * fs);
    Channels ch(static_cast<size_t>(set.size()), std::vector<double>(n, 0.0));
    for (int c = 0; c < set.size(); ++c)
    {
        const auto type = set.getTypeOfChannel(c);
        const double bed = type == juce::AudioChannelSet::LFE ? 0.05 : 0.1 + 0.01 * (static_cast<int>(type) % 7);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / fs;
            const double v = 0.1 * std::sin(2.0 * juce::MathConstants<double>::pi * (1000.0 + 100.0 * c) * t)
                           + bed * std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * t);
            ch[static_cast<size_t>(c)][i] = static_cast<double>(static_cast<float>(v));
        }
    }
    return ch;
}

inline double rmsDb(const std::vector<double>& y, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += y[i] * y[i];
    return 10.0 * std::log10(std::max(s / static_cast<double>(std::max<size_t>(1, to - from)), 1e-300));
}

// out - in delayed by `latency`: what the processor added to the dry
inline std::vector<double> added(const std::vector<double>& out, const std::vector<double>& in, size_t latency)
{
    std::vector<double> d(out.size());
    for (size_t i = 0; i < out.size(); ++i)
        d[i] = out[i] - (i >= latency ? in[i - latency] : 0.0);
    return d;
}
} // namespace subgen::test
