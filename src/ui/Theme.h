// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace subgen::ui
{
// The editor's palette and type (docs/superpowers/specs/2026-11-04-subgen-ui-design.md §2): deep-water blues, the
// sub's amber heating to ember where the sub is made, cool steel for the dry.
namespace colours
{
inline const juce::Colour background{0xff101826};
inline const juce::Colour panel{0xff162131};
inline const juce::Colour raised{0xff1d2a3d};
inline const juce::Colour line{0xff2a3a51};
inline const juce::Colour track{0xff243349};
inline const juce::Colour text{0xffdde4ee};
inline const juce::Colour textDim{0xff8796ab};
inline const juce::Colour textFaint{0xff56657a};
inline const juce::Colour sub{0xfff2a33a};
inline const juce::Colour ember{0xffff6a3d};
inline const juce::Colour dry{0xff7f9cc0};
inline const juce::Colour sum{0xffe9eef5};
inline const juce::Colour warning{0xffff5d5d};
inline const juce::Colour ok{0xff5bd1a5};
} // namespace colours

inline constexpr int kEditorWidth = 1000;
inline constexpr int kEditorHeight = 620;
inline constexpr double kMinScale = 0.75;
inline constexpr double kMaxScale = 2.0;

inline juce::Font uiFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

// the drive arc's colour: amber at rest heating to ember at full drive
inline juce::Colour heat(float amount) { return colours::sub.interpolatedWith(colours::ember, juce::jlimit(0.0f, 1.0f, amount)); }
} // namespace subgen::ui
