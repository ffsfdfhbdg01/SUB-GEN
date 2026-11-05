// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

namespace subgen::plugin
{
// The plug-in's state outside the parameters (PARAMETERS §6): the UI, the last Auto Align result and the preset's
// name. The A/B slots come with Phase 4 (<AB/> stays empty until then).
struct ExtraState
{
    double uiScale = 1.0;
    int uiPage = 0;
    int abSlot = 0; // 0: A, 1: B
    juce::String theme = "dark";
    int alignPolarity = 1;
    double alignRotationDeg = 0.0; // |theta| <= 90
    double alignConfidenceDb = 0.0;
    juce::String presetName = "Init";
    juce::String presetCategory = "Utility";
};

inline constexpr int kStateVersion = 1;

// SPEC_IO_UI §9: <SUBGEN stateVersion="1"> holding the APVTS tree's <PARAM id value/> elements, then <UI scale page
// abSlot theme/>, <AB/>, <ALIGN pol rot conf/> and <PRESET name category/>. Message thread (it flushes the tree).
std::unique_ptr<juce::XmlElement> makeState(juce::AudioProcessorValueTreeState& parameters, const ExtraState& extra);

// The chain vN -> vN+1 up to kStateVersion; v1 is the first, so for now it is only checked. nullptr: not a SUB-GEN
// state, no or an unknown version (a newer plug-in's state included).
std::unique_ptr<juce::XmlElement> migrate(std::unique_ptr<juce::XmlElement> state);

// Every registered parameter from the state when it holds a finite number for it (clamped to the range); a parameter
// the state lacks takes its neutral value when it was added after 0.1 (params::kAddedParams), otherwise its default;
// unknown ids are ignored; the values are set exactly as saved. A null state: every parameter and the extra
// state at their defaults.
void applyState(const juce::XmlElement* state, juce::AudioProcessorValueTreeState& parameters, ExtraState& extra);

// The host's binary state: the XML text compressed (zlib). Reading takes any bytes: nullptr when they hold no XML
// (corrupt, cut short, over 1 MB unpacked or nested deeper than a state can be).
juce::MemoryBlock compressState(const juce::XmlElement& state);
std::unique_ptr<juce::XmlElement> decompressState(const void* data, int sizeInBytes);
} // namespace subgen::plugin
