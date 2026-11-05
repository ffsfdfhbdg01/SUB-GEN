// SPDX-License-Identifier: AGPL-3.0-or-later
#include "routing/ChannelMap.h"

namespace subgen::routing
{
ChannelInfo classify(juce::AudioChannelSet::ChannelType type)
{
    using T = juce::AudioChannelSet;
    switch (type)
    {
    case T::left:
    case T::leftCentre:
    case T::wideLeft:
        return {ChannelClass::Front, Side::Left, -1};
    case T::right:
    case T::rightCentre:
    case T::wideRight:
        return {ChannelClass::Front, Side::Right, -1};
    case T::centre:
        return {ChannelClass::Center, Side::Centre, -1};
    case T::leftSurround:
    case T::leftSurroundSide:
    case T::leftSurroundRear:
        return {ChannelClass::Surround, Side::Left, -1};
    case T::rightSurround:
    case T::rightSurroundSide:
    case T::rightSurroundRear:
        return {ChannelClass::Surround, Side::Right, -1};
    case T::centreSurround:
        return {ChannelClass::Surround, Side::Centre, -1};
    case T::topFrontLeft:
    case T::topRearLeft:
    case T::topSideLeft:
        return {ChannelClass::Height, Side::Left, -1};
    case T::topFrontRight:
    case T::topRearRight:
    case T::topSideRight:
        return {ChannelClass::Height, Side::Right, -1};
    case T::topFrontCentre:
    case T::topRearCentre:
    case T::topMiddle:
        return {ChannelClass::Height, Side::Centre, -1};
    case T::LFE:
        return {ChannelClass::Lfe, Side::None, -1};
    default:
        return {ChannelClass::Other, Side::None, -1};
    }
}

ChannelMap buildChannelMap(const juce::AudioChannelSet& set, std::span<const int> bufferIndices, int bufferChannels)
{
    ChannelMap map;
    if (set.size() > static_cast<int>(map.ch.size()))
        return map; // over 32 channels: isSupportedLayout rejects the layout, nothing is mapped
    map.count = set.size();
    for (int c = 0; c < map.count; ++c)
    {
        auto& info = map.ch[static_cast<size_t>(c)];
        info = classify(set.getTypeOfChannel(c));
        const int index = static_cast<size_t>(c) < bufferIndices.size() ? bufferIndices[static_cast<size_t>(c)] : -1;
        if (index < 0 || index >= bufferChannels)
            info = {ChannelClass::Other, Side::None, -1}; // not in the block buffer: nobody's channel
        else
            info.bufferIndex = index;
        map.hasLfe = map.hasLfe || info.cls == ChannelClass::Lfe;
    }
    return map;
}

bool isSupportedLayout(const juce::AudioChannelSet& set)
{
    if (set.isDisabled() || set.size() > 32)
        return false;
    for (const auto type : set.getChannelTypes())
        if (classify(type).cls == ChannelClass::Other && type != juce::AudioChannelSet::LFE2)
            return false;
    return true;
}

bool isSupportedPair(const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
{
    if (!isSupportedLayout(in) || !isSupportedLayout(out))
        return false;
    if (in == out)
        return true;
    if (!in.getChannelTypes().contains(juce::AudioChannelSet::LFE) && out.getChannelTypes().contains(juce::AudioChannelSet::LFE))
    {
        auto withoutLfe = out;
        withoutLfe.removeChannel(juce::AudioChannelSet::LFE);
        if (withoutLfe == in)
            return true;
    }
    return in == juce::AudioChannelSet::mono()
        && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::create5point1()
            || out == juce::AudioChannelSet::create7point1());
}
} // namespace subgen::routing
