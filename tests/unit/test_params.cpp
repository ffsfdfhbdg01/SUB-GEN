// SPDX-License-Identifier: AGPL-3.0-or-later
// The parameter registry (PARAMETERS.md, generated into src/params/ParamSpecs.inc): the layout the host sees, defaults
// and ranges, the automation flags, Off gains and the relations sanitise() enforces.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>

#include "params/Parameters.h"
#include "params/Sanitise.h"
#include "plugin/PluginProcessor.h"

using namespace subgen;
using namespace subgen::params;

namespace
{
juce::String idOf(const juce::AudioProcessorParameter* p)
{
    return dynamic_cast<const juce::AudioProcessorParameterWithID&>(*p).paramID;
}

ParamSnapshot defaults()
{
    ParamSnapshot s;
    for (size_t i = 0; i < kParamSpecs.size(); ++i)
        s.raw[i] = static_cast<float>(kParamSpecs[i].def);
    return s;
}
} // namespace

TEST_CASE("layout has 141 parameters with unique ASCII ids of <= 24 chars and version hint 1", "[params]")
{
    plugin::SubGenProcessor processor;
    const auto& all = processor.getParameters();
    REQUIRE(all.size() == 141);
    CHECK(kNumParams == 141);
    std::set<juce::String> ids;
    for (int i = 0; i < all.size(); ++i)
    {
        const auto& p = dynamic_cast<const juce::AudioProcessorParameterWithID&>(*all[i]);
        INFO(p.paramID);
        CHECK(p.paramID.length() <= 24);
        CHECK(std::all_of(p.paramID.begin(), p.paramID.end(), [](juce::juce_wchar c) { return c < 128; }));
        CHECK(p.getVersionHint() == 1);
        CHECK(p.paramID == juce::String(kParamSpecs[static_cast<size_t>(i)].id)); // in ParamId order
        ids.insert(p.paramID);
    }
    CHECK(ids.size() == 141);
}

TEST_CASE("defaults and ranges equal the registry", "[params]")
{
    plugin::SubGenProcessor processor;
    for (const auto& s : kParamSpecs)
    {
        INFO(s.id);
        const auto* p = processor.parameters().getParameter(s.id);
        REQUIRE(p != nullptr);
        CHECK(p->getNormalisableRange().start == static_cast<float>(s.min));
        CHECK(p->getNormalisableRange().end == static_cast<float>(s.max));
        CHECK(std::abs(p->convertFrom0to1(p->getDefaultValue()) - static_cast<float>(s.def)) <= 1e-4f * (1.0f + std::abs(static_cast<float>(s.def))));
    }
    // rows straight from PARAMETERS.md (a float comes back through its normalised value: within 1e-5)
    ParamSnapshot snap;
    ParamView(processor.parameters()).read(snap);
    CHECK(std::abs(snap[ParamId::sub_level] + 6.0f) <= 1e-5f);
    CHECK(snap.index(ParamId::quality) == 1);      // Mix
    CHECK(snap.index(ParamId::comp_detect) == 0);  // Peak
    CHECK(snap.index(ParamId::sub_hp_slope) == 1); // 24 dB/oct
    CHECK(std::abs(snap[ParamId::eq1_freq] - 40.0f) <= 1e-4f);
    CHECK(snap.index(ParamId::eq5_type) == 2);     // High Shelf
    CHECK(snap.on(ParamId::lim_on));
    CHECK_FALSE(snap.on(ParamId::bypass));
    // the log scale puts the geometric middle at 0.5
    const auto* lp = processor.parameters().getParameter("sub_lp_freq");
    CHECK(std::abs(lp->convertFrom0to1(0.5f) - std::sqrt(30.0f * 500.0f)) <= 0.01f);
    // the view reads what the host set, in plain units
    auto* drive = processor.parameters().getParameter("drive");
    drive->setValueNotifyingHost(drive->convertTo0to1(37.5f));
    CHECK(std::abs(ParamView(processor.parameters()).get(ParamId::drive) - 37.5f) <= 1e-4f);
}

TEST_CASE("quality, filt_phase and align_ms are not automatable", "[params]")
{
    plugin::SubGenProcessor processor;
    for (const auto* p : processor.getParameters())
    {
        const auto id = idOf(p);
        INFO(id);
        CHECK(p->isAutomatable() == !(id == "quality" || id == "filt_phase" || id == "align_ms"));
    }
}

TEST_CASE("Off at -60 dB maps to gain 0.0 exactly", "[params]")
{
    CHECK(gainFromDb(-60.0, true) == 0.0);
    CHECK(gainFromDb(-60.0, false) == std::pow(10.0, -3.0));
    CHECK(gainFromDb(0.0, true) == 1.0);
    CHECK(std::abs(gainFromDb(-6.0, true) - 0.5011872336272722) <= 1e-15);
    auto s = defaults();
    s[ParamId::sub_level] = -60.0f;
    s[ParamId::gate_range] = -60.0f;
    CHECK(s.gain(ParamId::sub_level) == 0.0);                   // -60 = Off
    CHECK(s.gain(ParamId::gate_range) == std::pow(10.0, -3.0)); // a depth, not an Off gain
    CHECK(s.gain(ParamId::sub2_level) == 0.0);                  // its default, -60, is Off
    int offGains = 0;
    for (const auto& spec : kParamSpecs)
        offGains += spec.scale == Scale::LinDbOff ? 1 : 0;
    CHECK(offGains == 15); // feed x5, sub1, sub2, sub_level, dry, route x5, lfe_bleed
}

TEST_CASE("sanitise enforces range_hi >= 1.2 range_lo, lfe_lp >= 2 lfe_hp, Bessel slopes <= 48, Linear Phase only in Print",
          "[params]")
{
    auto s = defaults();
    s[ParamId::range_lo] = 200.0f;
    s[ParamId::range_hi] = 100.0f;
    s[ParamId::lfe_hp_freq] = 40.0f;
    s[ParamId::lfe_lp_freq] = 60.0f;
    s[ParamId::filt_type] = static_cast<float>(choiceIndex(ParamId::filt_type, "Bessel"));
    s[ParamId::sub_hp_slope] = static_cast<float>(choiceIndex(ParamId::sub_hp_slope, "96 dB/oct"));
    s[ParamId::sub_lp_slope] = static_cast<float>(choiceIndex(ParamId::sub_lp_slope, "96 dB/oct"));
    s[ParamId::filt_phase] = static_cast<float>(choiceIndex(ParamId::filt_phase, "Linear Phase"));
    sanitise(s); // quality: Mix
    CHECK(s[ParamId::range_hi] == 100.0f); // the first parameter of a relation wins
    CHECK(s[ParamId::range_hi] >= 1.2f * s[ParamId::range_lo] * (1.0f - 1e-6f));
    CHECK(std::abs(s[ParamId::range_lo] - 100.0f / 1.2f) <= 1e-4f);
    CHECK(s[ParamId::lfe_lp_freq] == 60.0f);
    CHECK(s[ParamId::lfe_hp_freq] == 30.0f);
    CHECK(s.index(ParamId::sub_hp_slope) == choiceIndex(ParamId::sub_hp_slope, "48 dB/oct"));
    CHECK(s.index(ParamId::sub_lp_slope) == choiceIndex(ParamId::sub_lp_slope, "48 dB/oct"));
    CHECK(s.index(ParamId::filt_phase) == choiceIndex(ParamId::filt_phase, "Min Phase"));
    // in Print Linear Phase stays, with Butterworth 96 dB/oct stays; values inside the relations are untouched
    auto print = defaults();
    print[ParamId::quality] = static_cast<float>(choiceIndex(ParamId::quality, "Print"));
    print[ParamId::filt_phase] = static_cast<float>(choiceIndex(ParamId::filt_phase, "Linear Phase"));
    print[ParamId::sub_hp_slope] = static_cast<float>(choiceIndex(ParamId::sub_hp_slope, "96 dB/oct"));
    const auto before = print;
    sanitise(print);
    CHECK(print.raw == before.raw);
    auto plain = defaults();
    const auto untouched = plain;
    sanitise(plain);
    CHECK(plain.raw == untouched.raw);
}
