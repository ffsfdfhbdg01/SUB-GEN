// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Format.h"

#include <cmath>

namespace subgen::ui
{
namespace
{
using params::ParamId;
using params::ParamType;

const juce::String kMinus = juce::String::fromUTF8("\xe2\x88\x92");

juce::StringArray labelsOf(ParamId id)
{
    return juce::StringArray::fromTokens(juce::String::fromUTF8(params::spec(id).choices), "|", "");
}

// a signed number with a true minus; `plus` puts "+" on positive values
juce::String number(double v, int decimals, bool plus)
{
    const double rounded = std::round(v * std::pow(10.0, decimals)) / std::pow(10.0, decimals);
    juce::String s = juce::String(std::abs(rounded), decimals);
    if (rounded < 0.0)
        return kMinus + s;
    return (plus && rounded > 0.0 ? "+" : "") + s;
}
} // namespace

juce::String formatValue(ParamId id, double plain)
{
    const auto& s = params::spec(id);
    if (s.type != ParamType::Float)
    {
        const auto labels = labelsOf(id);
        const int index = juce::jlimit(0, labels.size() - 1, static_cast<int>(std::lround(plain)));
        return labels[index];
    }
    const juce::String unit = juce::String::fromUTF8(s.unit);
    if (s.scale == params::Scale::LinDbOff && plain <= s.min + 1e-6)
        return "Off";
    if (unit == "dB" || unit == "dBFS")
        return number(plain, 1, unit == "dB") + " " + unit;
    if (unit == "Hz")
    {
        if (plain >= 1000.0)
            return juce::String(plain / 1000.0, 2) + " kHz";
        return plain < 100.0 ? juce::String(plain, 1) + " Hz" : juce::String(std::lround(plain)) + " Hz";
    }
    if (unit == "ms")
    {
        if (plain >= 1000.0)
            return juce::String(plain / 1000.0, 2) + " s";
        if (plain < 10.0)
            return juce::String(plain, 2) + " ms";
        return plain < 100.0 ? juce::String(plain, 1) + " ms" : juce::String(std::lround(plain)) + " ms";
    }
    if (unit == "%")
        return number(plain, 0, s.min < 0.0) + " %";
    if (unit == juce::String::fromUTF8("\xc2\xb0"))
        return number(plain, 0, false) + unit;
    if (unit == ":1")
        return juce::String(plain, 1) + ":1";
    return juce::String(plain, 2);
}

std::optional<double> parseValue(ParamId id, const juce::String& text)
{
    const auto& s = params::spec(id);
    const auto t = text.trim().replace(kMinus, "-");
    if (s.type != ParamType::Float)
    {
        const auto labels = labelsOf(id);
        for (int i = 0; i < labels.size(); ++i)
            if (labels[i].equalsIgnoreCase(t))
                return static_cast<double>(i);
        return std::nullopt;
    }
    if (s.scale == params::Scale::LinDbOff && t.equalsIgnoreCase("off"))
        return s.min;
    const auto numeric = t.initialSectionContainingOnly("+-.0123456789");
    if (numeric.isEmpty() || !numeric.containsAnyOf("0123456789"))
        return std::nullopt;
    double v = numeric.getDoubleValue();
    const auto rest = t.substring(numeric.length()).trim().toLowerCase();
    const juce::String unit = juce::String::fromUTF8(s.unit);
    if (unit == "Hz" && rest.startsWith("k"))
        v *= 1000.0;
    else if (unit == "ms" && rest.startsWith("s"))
        v *= 1000.0;
    return juce::jlimit(s.min, s.max, v);
}

juce::String noteName(double hz)
{
    if (!(hz > 0.0) || !std::isfinite(hz))
        return {};
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
    const long nearest = std::lround(midi);
    const int cents = static_cast<int>(std::lround((midi - static_cast<double>(nearest)) * 100.0));
    const int octave = static_cast<int>(std::floor(static_cast<double>(nearest) / 12.0)) - 1;
    const auto note = static_cast<size_t>(((nearest % 12) + 12) % 12);
    return juce::String(names[note]) + juce::String(octave) + " " + (cents < 0 ? kMinus : juce::String("+"))
         + juce::String(std::abs(cents)) + " ct";
}
} // namespace subgen::ui
