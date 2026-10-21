// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugin/PluginProcessor.h"

namespace subgen::plugin
{
SubGenProcessor::SubGenProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void SubGenProcessor::prepareToPlay(double, int)
{
    setLatencySamples(0);
}

void SubGenProcessor::releaseResources() {}

bool SubGenProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// Pass-through: the host's buffer already holds the input, which is the output.
void SubGenProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) {}

void SubGenProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) {}

juce::AudioProcessorEditor* SubGenProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

const juce::String SubGenProcessor::getName() const
{
    return JucePlugin_Name;
}

void SubGenProcessor::getStateInformation(juce::MemoryBlock&) {}

void SubGenProcessor::setStateInformation(const void*, int) {}
} // namespace subgen::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new subgen::plugin::SubGenProcessor();
}
