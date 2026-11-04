// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <optional>

#include <juce_core/juce_core.h>

#include "params/Parameters.h"

namespace subgen::ui
{
// A parameter's plain value as the editor shows it, with its unit: "−6.0 dB" (a true minus), "+3.0 dB", "Off" at a
// LinDbOff minimum, "45.0 Hz" / "140 Hz" / "1.20 kHz", "3.00 ms" / "80.0 ms" / "350 ms" / "1.50 s", "42 %",
// "−90°", "3.0:1", a choice's or a bool's label.
juce::String formatValue(params::ParamId id, double plain);

// Text a person typed back to a plain value, clamped to the range: a number with an optional unit or "k" (kHz) or "s"
// (seconds for a ms parameter), either minus sign, "off" for a LinDbOff gain, a label (any case) for a choice or a
// bool. nullopt when it reads as none of these.
std::optional<double> parseValue(params::ParamId id, const juce::String& text);

// The nearest equal-tempered note and the distance to it: "A1 +0 ct", "E1 +38 ct"; empty for no frequency.
juce::String noteName(double hz);
} // namespace subgen::ui
