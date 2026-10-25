// SPDX-License-Identifier: AGPL-3.0-or-later
#include "params/Parameters.h"

namespace subgen::params
{
namespace
{
juce::NormalisableRange<float> rangeOf(const ParamSpec& s)
{
    const auto lo = static_cast<float>(s.min);
    const auto hi = static_cast<float>(s.max);
    if (s.scale != Scale::Log)
        return {lo, hi};
    return {lo, hi, [](float start, float end, float t) { return start * std::pow(end / start, t); },
            [](float start, float end, float v) { return std::log(v / start) / std::log(end / start); },
            [](float start, float end, float v) { return juce::jlimit(start, end, v); }};
}

juce::StringArray labelsOf(const ParamSpec& s)
{
    return juce::StringArray::fromTokens(juce::String::fromUTF8(s.choices), "|", "");
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& s : kParamSpecs)
    {
        const juce::ParameterID id{s.id, 1};
        const juce::String name{s.name};
        switch (s.type)
        {
        case ParamType::Float:
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                id, name, rangeOf(s), static_cast<float>(s.def),
                juce::AudioParameterFloatAttributes().withAutomatable(s.automatable).withLabel(juce::String::fromUTF8(s.unit))));
            break;
        case ParamType::Choice:
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                id, name, labelsOf(s), static_cast<int>(s.def),
                juce::AudioParameterChoiceAttributes().withAutomatable(s.automatable)));
            break;
        case ParamType::Bool:
        {
            const auto labels = labelsOf(s);
            layout.add(std::make_unique<juce::AudioParameterBool>(
                id, name, s.def >= 0.5,
                juce::AudioParameterBoolAttributes()
                    .withAutomatable(s.automatable)
                    .withStringFromValueFunction([labels](bool v, int) { return labels[v ? 1 : 0]; })
                    .withValueFromStringFunction([labels](const juce::String& text) { return text == labels[1]; })));
            break;
        }
        }
    }
    return layout;
}

ParamView::ParamView(const juce::AudioProcessorValueTreeState& state)
{
    for (size_t i = 0; i < kParamSpecs.size(); ++i)
    {
        values[i] = state.getRawParameterValue(kParamSpecs[i].id);
        jassert(values[i] != nullptr);
    }
}

void ParamView::read(ParamSnapshot& snapshot) const
{
    for (size_t i = 0; i < values.size(); ++i)
        snapshot.raw[i] = values[i]->load(std::memory_order_relaxed);
}
} // namespace subgen::params
