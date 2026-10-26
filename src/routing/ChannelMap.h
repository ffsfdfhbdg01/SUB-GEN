// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include <juce_audio_basics/juce_audio_basics.h>

namespace subgen::routing
{
// The five channel classes of SPEC_IO_UI §1, in the order of their parameters (feed_*, route_*); Other for LFE2
// (passed through), discrete and unknown channels.
enum class ChannelClass
{
    Front,
    Center,
    Surround,
    Height,
    Lfe,
    Other
};

enum class Side
{
    Left,
    Right,
    Centre,
    None
};

inline constexpr size_t kNumClasses = 5;

struct ChannelInfo
{
    ChannelClass cls = ChannelClass::Other;
    Side side = Side::None;
    int bufferIndex = -1;
};

// A layout's channels by class, built in prepareToPlay so that the audio thread looks nothing up (SPEC_IO_UI §2.3).
struct ChannelMap
{
    std::array<ChannelInfo, 32> ch{};
    int count = 0;
    bool hasLfe = false;
};

ChannelInfo classify(juce::AudioChannelSet::ChannelType type);

// Channel c of the set sits at bufferIndices[c] of a block buffer of bufferChannels channels
// (getChannelIndexInProcessBlockBuffer); a missing or out-of-range index makes the channel Other with bufferIndex -1,
// which no stage reads or writes. A set of over 32 channels maps nothing.
ChannelMap buildChannelMap(const juce::AudioChannelSet& set, std::span<const int> bufferIndices, int bufferChannels);

// SPEC_IO_UI §2.1: every channel is in a class of §1 (LFE2 passes through); ambisonic, discrete and unknown are not.
bool isSupportedLayout(const juce::AudioChannelSet& set);

// SPEC_IO_UI §2.2: in == out; an input without LFE into the same channels plus an LFE (x.0 -> x.1); mono into stereo,
// 5.1 or 7.1 (wet only).
bool isSupportedPair(const juce::AudioChannelSet& in, const juce::AudioChannelSet& out);
} // namespace subgen::routing
