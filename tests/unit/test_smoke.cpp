// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "plugin/PluginProcessor.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

template <typename Sample>
juce::AudioBuffer<Sample> sine60Hz(int block)
{
    juce::AudioBuffer<Sample> buffer(2, kBlockSize);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i,
                             static_cast<Sample>(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 60.0
                                                                * (block * kBlockSize + i) / kSampleRate)));
    return buffer;
}

template <typename Sample>
bool finiteAndSounding(const juce::AudioBuffer<Sample>& buffer)
{
    double peak = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double v = static_cast<double>(buffer.getSample(ch, i));
            if (!std::isfinite(v))
                return false;
            peak = std::max(peak, std::abs(v));
        }
    return peak > 0.1;
}

template <typename Sample>
bool rendersAfterLatency(subgen::plugin::SubGenProcessor& processor)
{
    juce::MidiBuffer midi;
    const int blocks = processor.getLatencySamples() / kBlockSize + 2;
    juce::AudioBuffer<Sample> buffer;
    for (int b = 0; b < blocks; ++b)
    {
        buffer = sine60Hz<Sample>(b);
        processor.processBlock(buffer, midi);
    }
    return finiteAndSounding(buffer);
}
} // namespace

// Task 1's skeleton passed audio through; the processor (Task 25) delays the dry by the reported latency and adds the
// sub, which T-SUM-01 and T-LAT-01 check exactly. This stays the quickest end-to-end check of both precisions.
TEST_CASE("processor renders finite float and double audio after its reported latency", "[smoke]")
{
    subgen::plugin::SubGenProcessor processor;
    processor.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    CHECK(processor.getLatencySamples() == processor.latencyBreakdown().total);
    CHECK(processor.getLatencySamples() > 0); // Mix by default: look-ahead and oversampling

    SECTION("float")
    {
        CHECK(rendersAfterLatency<float>(processor));
    }

    SECTION("double")
    {
        REQUIRE(processor.supportsDoublePrecisionProcessing());
        processor.setProcessingPrecision(juce::AudioProcessor::doublePrecision);
        CHECK(rendersAfterLatency<double>(processor));
    }

    processor.releaseResources();
}
