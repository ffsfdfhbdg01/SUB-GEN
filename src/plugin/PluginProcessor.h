// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "params/Parameters.h"

namespace subgen::plugin
{
// The SUB-GEN audio processor. Phase 3 skeleton: stereo in/out, passes audio through unchanged, no latency.
class SubGenProcessor final : public juce::AudioProcessor
{
public:
    SubGenProcessor();

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // the parameter registry (PARAMETERS.md)
    juce::AudioProcessorValueTreeState& parameters() { return state; }
    const juce::AudioProcessorValueTreeState& parameters() const { return state; }

private:
    juce::AudioProcessorValueTreeState state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SubGenProcessor)
};
} // namespace subgen::plugin
