// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "plugin/PluginProcessor.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

template <typename Sample>
juce::AudioBuffer<Sample> sine60Hz()
{
    juce::AudioBuffer<Sample> buffer(2, kBlockSize);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i,
                             static_cast<Sample>(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * i / kSampleRate)));
    return buffer;
}

template <typename Sample>
bool identical(const juce::AudioBuffer<Sample>& a, const juce::AudioBuffer<Sample>& b)
{
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample(ch, i) != b.getSample(ch, i))
                return false;
    return true;
}
} // namespace

TEST_CASE("processor passes audio through unchanged", "[smoke]")
{
    subgen::plugin::SubGenProcessor processor;
    processor.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
    juce::MidiBuffer midi;

    SECTION("float")
    {
        auto buffer = sine60Hz<float>();
        const auto input = buffer;
        processor.processBlock(buffer, midi);
        CHECK(identical(buffer, input));
    }

    SECTION("double")
    {
        REQUIRE(processor.supportsDoublePrecisionProcessing());
        processor.setProcessingPrecision(juce::AudioProcessor::doublePrecision);
        auto buffer = sine60Hz<double>();
        const auto input = buffer;
        processor.processBlock(buffer, midi);
        CHECK(identical(buffer, input));
    }

    CHECK(processor.getLatencySamples() == 0);
    processor.releaseResources();
}
