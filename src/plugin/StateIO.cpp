// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugin/StateIO.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "params/Parameters.h"

namespace subgen::plugin
{
namespace
{
constexpr size_t kMaxStateBytes = 1 << 20; // unpacked; a v1 state is about 8 KB
constexpr int kMaxDepth = 8;               // a v1 state nests 2 deep; JUCE's parser recurses once per level
constexpr int kMaxText = 256;              // the extra state's strings

// A whole attribute as a finite number, read as JUCE reads numbers (the "C" locale, whatever the host set);
// "", "abc", "1.5.2", "nan" and "1e400" are none.
std::optional<double> number(const juce::String& text)
{
    auto p = text.getCharPointer().findEndOfWhitespace();
    const auto start = p;
    const double v = juce::CharacterFunctions::readDoubleValue(p);
    if (p == start || !p.findEndOfWhitespace().isEmpty() || !std::isfinite(v))
        return std::nullopt;
    return v;
}

double attribute(const juce::XmlElement& e, const char* name, double lo, double hi, double fallback)
{
    const auto v = number(e.getStringAttribute(name));
    return v ? std::clamp(*v, lo, hi) : fallback;
}

// The deepest element nesting of XML text, read without parsing: comments, declarations and quoted attribute values
// are skipped, self-closing elements do not nest.
int nestingDepth(const char* s, size_t n)
{
    int depth = 0, deepest = 0;
    size_t i = 0;
    while (i < n)
    {
        if (s[i] != '<')
        {
            ++i;
            continue;
        }
        if (i + 3 < n && s[i + 1] == '!' && s[i + 2] == '-' && s[i + 3] == '-') // a comment
        {
            static constexpr char kEnd[] = {'-', '-', '>'};
            const char* end = std::search(s + i + 4, s + n, kEnd, kEnd + 3);
            i = static_cast<size_t>(end - s) + 3;
            continue;
        }
        const bool declaration = i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!');
        const bool closing = i + 1 < n && s[i + 1] == '/';
        size_t j = i + 1;
        char quote = 0;
        while (j < n && (quote != 0 || s[j] != '>'))
        {
            if (quote != 0)
                quote = s[j] == quote ? 0 : quote;
            else if (s[j] == '"' || s[j] == '\'')
                quote = s[j];
            ++j;
        }
        const bool selfClosing = j < n && s[j - 1] == '/';
        if (closing)
            --depth;
        else if (!declaration && !selfClosing)
            deepest = std::max(deepest, ++depth);
        i = j + 1;
    }
    return deepest;
}

// Sets a parameter so that the APVTS reads `target` exactly: the parameter stores convertFrom0to1(n) and the APVTS
// reads that back through convertTo0to1 and convertFrom0to1, so the normalised value is the one nearest
// convertTo0to1(target) (within 16 steps of a float) that lands on it, else that one. The APVTS ignores a change within
// a float epsilon of its value, so such a change first steps away.
void setExactly(juce::RangedAudioParameter& param, const std::atomic<float>& value, float target)
{
    auto reads = [&](float n) { return param.convertFrom0to1(param.convertTo0to1(param.convertFrom0to1(n))); };
    const float first = param.convertTo0to1(target);
    float n = first;
    float up = first, down = first;
    for (int k = 0; k < 16 && !juce::exactlyEqual(reads(n), target); ++k)
    {
        up = std::min(std::nextafter(up, 2.0f), 1.0f);
        down = std::max(std::nextafter(down, -1.0f), 0.0f);
        n = juce::exactlyEqual(reads(up), target) ? up : down;
    }
    if (!juce::exactlyEqual(reads(n), target))
        n = first;
    const float now = value.load();
    if (!juce::exactlyEqual(now, target) && juce::approximatelyEqual(now, target))
        param.setValueNotifyingHost(n < 0.5f ? 1.0f : 0.0f);
    param.setValueNotifyingHost(n);
}

ExtraState extraFrom(const juce::XmlElement* state)
{
    ExtraState e;
    if (state == nullptr)
        return e;
    if (const auto* ui = state->getChildByName("UI"))
    {
        e.uiScale = attribute(*ui, "scale", 0.5, 4.0, e.uiScale);
        e.uiPage = static_cast<int>(std::lround(attribute(*ui, "page", 0.0, 16.0, e.uiPage)));
        e.abSlot = static_cast<int>(std::lround(attribute(*ui, "abSlot", 0.0, 1.0, e.abSlot)));
        e.theme = ui->getStringAttribute("theme", e.theme).substring(0, kMaxText);
    }
    if (const auto* align = state->getChildByName("ALIGN"))
    {
        e.alignPolarity = attribute(*align, "pol", -1.0, 1.0, 1.0) < 0.0 ? -1 : 1;
        e.alignRotationDeg = attribute(*align, "rot", -90.0, 90.0, e.alignRotationDeg);
        e.alignConfidenceDb = attribute(*align, "conf", 0.0, 1000.0, e.alignConfidenceDb);
    }
    if (const auto* preset = state->getChildByName("PRESET"))
    {
        e.presetName = preset->getStringAttribute("name", e.presetName).substring(0, kMaxText);
        e.presetCategory = preset->getStringAttribute("category", e.presetCategory).substring(0, kMaxText);
    }
    return e;
}
} // namespace

std::unique_ptr<juce::XmlElement> makeState(juce::AudioProcessorValueTreeState& parameters, const ExtraState& extra)
{
    auto xml = parameters.copyState().createXml(); // flushes the parameters into the tree first
    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement>("SUBGEN");
    xml->setTagName("SUBGEN");
    xml->setAttribute("stateVersion", kStateVersion);
    auto* ui = xml->createNewChildElement("UI");
    ui->setAttribute("scale", extra.uiScale);
    ui->setAttribute("page", extra.uiPage);
    ui->setAttribute("abSlot", extra.abSlot);
    ui->setAttribute("theme", extra.theme);
    xml->createNewChildElement("AB"); // the A/B slots: Phase 4
    auto* align = xml->createNewChildElement("ALIGN");
    align->setAttribute("pol", extra.alignPolarity);
    align->setAttribute("rot", extra.alignRotationDeg);
    align->setAttribute("conf", extra.alignConfidenceDb);
    auto* preset = xml->createNewChildElement("PRESET");
    preset->setAttribute("name", extra.presetName);
    preset->setAttribute("category", extra.presetCategory);
    return xml;
}

std::unique_ptr<juce::XmlElement> migrate(std::unique_ptr<juce::XmlElement> state)
{
    if (state == nullptr || !state->hasTagName("SUBGEN"))
        return nullptr;
    const auto version = number(state->getStringAttribute("stateVersion"));
    if (!version || *version != kStateVersion)
        return nullptr;
    // the steps vN -> vN+1 go here, the oldest first (PARAMETERS §5: a new parameter enters at its neutral value)
    return state;
}

void applyState(const juce::XmlElement* state, juce::AudioProcessorValueTreeState& parameters, ExtraState& extra)
{
    std::map<juce::String, double> saved; // the first value of an id counts
    if (state != nullptr)
        for (const auto* p : state->getChildWithTagNameIterator("PARAM"))
            if (const auto v = number(p->getStringAttribute("value")))
                saved.emplace(p->getStringAttribute("id"), *v);
    for (const auto& spec : params::kParamSpecs)
    {
        auto* param = parameters.getParameter(spec.id);
        const auto* value = parameters.getRawParameterValue(spec.id);
        if (param == nullptr || value == nullptr)
            continue;
        const auto& range = param->getNormalisableRange();
        const auto it = saved.find(spec.id);
        const float target = it != saved.end()
                                 ? static_cast<float>(std::clamp(it->second, static_cast<double>(range.start),
                                                                 static_cast<double>(range.end)))
                                 : range.convertFrom0to1(param->getDefaultValue()); // as the APVTS starts it
        setExactly(*param, *value, target);
    }
    extra = extraFrom(state);
}

juce::MemoryBlock compressState(const juce::XmlElement& state)
{
    juce::MemoryBlock block;
    {
        juce::MemoryOutputStream out(block, false);
        juce::GZIPCompressorOutputStream zip(out, 9);
        const auto text = state.toString(juce::XmlElement::TextFormat().singleLine());
        zip.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
        zip.flush();
    }
    return block;
}

std::unique_ptr<juce::XmlElement> decompressState(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return nullptr;
    juce::MemoryInputStream in(data, static_cast<size_t>(sizeInBytes), false);
    juce::GZIPDecompressorInputStream unzip(in);
    juce::MemoryBlock text;
    char buffer[4096];
    for (;;)
    {
        const int n = unzip.read(buffer, static_cast<int>(sizeof(buffer)));
        if (n <= 0)
            break;
        text.append(buffer, static_cast<size_t>(n));
        if (text.getSize() > kMaxStateBytes)
            return nullptr;
    }
    const auto* chars = static_cast<const char*>(text.getData());
    if (text.isEmpty() || !juce::CharPointer_UTF8::isValidString(chars, static_cast<int>(text.getSize()))
        || nestingDepth(chars, text.getSize()) > kMaxDepth)
        return nullptr;
    return juce::parseXML(juce::String::fromUTF8(chars, static_cast<int>(text.getSize())));
}
} // namespace subgen::plugin
