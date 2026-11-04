// SPDX-License-Identifier: AGPL-3.0-or-later
// The editor (docs/superpowers/specs/2026-11-04-subgen-ui-design.md): value text, the analyser, where every parameter
// lives, the UI feed, undo, and the editor itself built, scaled and painted without a screen.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "ProcessorHarness.h"
#include "plugin/ParamUndo.h"
#include "plugin/UiFeed.h"
#include "ui/Editor.h"
#include "ui/Format.h"
#include "ui/Spectrum.h"
#include "ui/UiLayout.h"

using subgen::params::ParamId;
namespace ui = subgen::ui;

namespace
{
constexpr double kPi = juce::MathConstants<double>::pi;

std::vector<double> sine(double hz, double amplitude, double fs, size_t n)
{
    std::vector<double> x(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs);
    return x;
}
} // namespace

TEST_CASE("Values read with their units", "[ui][format]")
{
    CHECK(ui::formatValue(ParamId::sub_level, -6.0) == juce::String::fromUTF8("\xe2\x88\x92" "6.0 dB"));
    CHECK(ui::formatValue(ParamId::boost_gain, 3.0) == "+3.0 dB");
    CHECK(ui::formatValue(ParamId::out_trim, 0.0) == "0.0 dB");
    CHECK(ui::formatValue(ParamId::sub2_level, -60.0) == "Off");
    CHECK(ui::formatValue(ParamId::det_thresh, -55.0) == juce::String::fromUTF8("\xe2\x88\x92" "55.0 dBFS"));
    CHECK(ui::formatValue(ParamId::base_freq, 45.0) == "45.0 Hz");
    CHECK(ui::formatValue(ParamId::sub_lp_freq, 140.0) == "140 Hz");
    CHECK(ui::formatValue(ParamId::drive_tone, 1200.0) == "1.20 kHz");
    CHECK(ui::formatValue(ParamId::env_attack, 3.0) == "3.00 ms");
    CHECK(ui::formatValue(ParamId::env_release, 80.0) == "80.0 ms");
    CHECK(ui::formatValue(ParamId::trig_decay, 350.0) == "350 ms");
    CHECK(ui::formatValue(ParamId::trig_decay, 1500.0) == "1.50 s");
    CHECK(ui::formatValue(ParamId::drive, 42.4) == "42 %");
    CHECK(ui::formatValue(ParamId::dynamics, 20.0) == "+20 %");
    CHECK(ui::formatValue(ParamId::sub_rot, -90.0) == juce::String::fromUTF8("\xe2\x88\x92" "90\xc2\xb0"));
    CHECK(ui::formatValue(ParamId::comp_ratio, 3.0) == "3.0:1");
    CHECK(ui::formatValue(ParamId::boost_q, 1.0) == "1.00");
    CHECK(ui::formatValue(ParamId::engine, 3.0) == "Harmonic");
    CHECK(ui::formatValue(ParamId::sub_pol, 1.0) == juce::String::fromUTF8("180\xc2\xb0"));
}

TEST_CASE("Typed values parse back, clamped to the range", "[ui][format]")
{
    CHECK(ui::parseValue(ParamId::sub_level, "-6") == Catch::Approx(-6.0));
    CHECK(ui::parseValue(ParamId::sub_level, juce::String::fromUTF8("\xe2\x88\x92" "6.0 dB")) == Catch::Approx(-6.0));
    CHECK(ui::parseValue(ParamId::sub_level, "off") == Catch::Approx(-60.0));
    CHECK(ui::parseValue(ParamId::sub_level, "99") == Catch::Approx(18.0));
    CHECK(ui::parseValue(ParamId::drive_tone, "1.2 kHz") == Catch::Approx(1200.0));
    CHECK(ui::parseValue(ParamId::drive_tone, "1.2k") == Catch::Approx(1200.0));
    CHECK(ui::parseValue(ParamId::trig_decay, "1.5 s") == Catch::Approx(1500.0));
    CHECK(ui::parseValue(ParamId::engine, "track") == Catch::Approx(1.0));
    CHECK(ui::parseValue(ParamId::sub_pol, juce::String::fromUTF8("180\xc2\xb0")) == Catch::Approx(1.0));
    CHECK_FALSE(ui::parseValue(ParamId::sub_level, "loud").has_value());
    CHECK_FALSE(ui::parseValue(ParamId::engine, "Spectral").has_value());
}

TEST_CASE("Note names with cents", "[ui][format]")
{
    CHECK(ui::noteName(440.0) == "A4 +0 ct");
    CHECK(ui::noteName(55.0) == "A1 +0 ct");
    CHECK(ui::noteName(43.65) == "F1 +0 ct");
    CHECK(ui::noteName(42.0).startsWith("E1 +"));
    CHECK(ui::noteName(0.0).isEmpty());
}

TEST_CASE("The analyser reads a full-scale sine at 0 dB where it is", "[ui][spectrum]")
{
    constexpr double fs = 48000.0;
    ui::Spectrum spectrum;
    spectrum.prepare(fs);
    const auto x = sine(50.0, 0.5, fs, 3 * ui::Spectrum::kSize);
    std::vector<float> f(x.begin(), x.end());
    spectrum.push(f.data(), static_cast<int>(f.size()));
    REQUIRE(spectrum.consumeUpdate());
    CHECK_FALSE(spectrum.consumeUpdate());
    const auto& db = spectrum.levelsDb();
    const auto peak = static_cast<int>(std::max_element(db.begin(), db.end()) - db.begin());
    CHECK(std::abs(std::log2(ui::Spectrum::pointHz(peak) / 50.0)) < 1.0 / 6.0);
    CHECK(db[static_cast<size_t>(peak)] == Catch::Approx(-6.02).margin(2.0));
    int at200 = 0;
    while (ui::Spectrum::pointHz(at200) < 200.0)
        ++at200;
    CHECK(db[static_cast<size_t>(at200)] < -60.0f);
    CHECK(ui::Spectrum::pointHz(0) == Catch::Approx(10.0));
    CHECK(ui::Spectrum::pointHz(ui::Spectrum::kPoints - 1) == Catch::Approx(500.0));
}

TEST_CASE("The analyser of silence sits at its floor", "[ui][spectrum]")
{
    ui::Spectrum spectrum;
    spectrum.prepare(44100.0);
    std::vector<float> zeros(ui::Spectrum::kSize * 2, 0.0f);
    spectrum.push(zeros.data(), static_cast<int>(zeros.size()));
    REQUIRE(spectrum.consumeUpdate());
    for (float v : spectrum.levelsDb())
        CHECK(v == ui::Spectrum::kFloorDb);
}

TEST_CASE("Every parameter has exactly one place in the editor", "[ui][layout]")
{
    std::map<int, int> placed;
    for (auto id : ui::topBarParams())
        ++placed[static_cast<int>(id)];
    for (const auto& section : ui::mainSections())
        for (auto id : section.ids)
            ++placed[static_cast<int>(id)];
    for (const auto& page : ui::advancedPages())
        for (const auto& group : page.groups)
            for (auto id : group.ids)
                ++placed[static_cast<int>(id)];
    for (int i = 0; i < subgen::params::kNumParams; ++i)
    {
        INFO(subgen::params::kParamSpecs[static_cast<size_t>(i)].id);
        CHECK(placed[i] == 1);
    }
    CHECK(placed.size() == static_cast<size_t>(subgen::params::kNumParams));
}

TEST_CASE("The engine page shows the active engine's own controls only", "[ui][layout]")
{
    const auto& pages = ui::advancedPages();
    const auto engine = std::find_if(pages.begin(), pages.end(), [](const auto& p) { return juce::String(p.name) == "Engine"; });
    REQUIRE(engine != pages.end());
    auto visibleIds = [&](int engineIndex) {
        std::set<int> ids;
        for (const auto& g : engine->groups)
            if (ui::groupVisible(g, engineIndex))
                for (auto id : g.ids)
                    ids.insert(static_cast<int>(id));
        return ids;
    };
    const auto divide = visibleIds(0), track = visibleIds(1), trigger = visibleIds(2), harmonic = visibleIds(3),
               hybrid = visibleIds(4);
    CHECK(divide.count(static_cast<int>(ParamId::conf_gate)) == 1);
    CHECK(divide.count(static_cast<int>(ParamId::trk_speed)) == 0);
    CHECK(track.count(static_cast<int>(ParamId::trk_speed)) == 1);
    CHECK(track.count(static_cast<int>(ParamId::trig_decay)) == 0);
    CHECK(trigger.count(static_cast<int>(ParamId::trig_decay)) == 1);
    CHECK(harmonic.count(static_cast<int>(ParamId::harm_amount)) == 1);
    CHECK(harmonic.count(static_cast<int>(ParamId::hyb_divide)) == 0);
    CHECK(hybrid.count(static_cast<int>(ParamId::hyb_divide)) == 1);
    CHECK(hybrid.count(static_cast<int>(ParamId::harm_amount)) == 1);
}

TEST_CASE("The UI feed costs nothing while the editor is closed", "[ui][feed]")
{
    auto p = subgen::test::makeProcessor(subgen::test::Layout{});
    REQUIRE(p != nullptr);
    auto& feed = p->uiFeed();
    const auto x = sine(60.0, 0.5, 48000.0, 4096);
    subgen::test::render(*p, {x, x});
    std::array<std::vector<float>, subgen::plugin::UiFeed::kLanes> lanes;
    std::array<float*, subgen::plugin::UiFeed::kLanes> dst{};
    for (size_t l = 0; l < lanes.size(); ++l)
        dst[l] = (lanes[l] = std::vector<float>(8192)).data();
    CHECK(feed.read(dst, 8192) == 0);

    feed.setEditorOpen(true);
    subgen::test::render(*p, {x, x});
    REQUIRE(feed.read(dst, 8192) == 4096);
    const auto& dry = lanes[subgen::plugin::UiFeed::Dry];
    const auto& out = lanes[subgen::plugin::UiFeed::OutPeak];
    CHECK(*std::max_element(dry.begin(), dry.begin() + 4096) > 0.3f);
    CHECK(*std::max_element(out.begin(), out.begin() + 4096) > 0.3f);
    CHECK(*std::min_element(out.begin(), out.begin() + 4096) >= 0.0f);

    feed.setEditorOpen(false);
    subgen::test::render(*p, {x, x});
    CHECK(feed.read(dst, 8192) == 0);
}

TEST_CASE("Undo takes back the editor's own changes, a quick run of one control as one step", "[ui][undo]")
{
    auto p = subgen::test::makeProcessor(subgen::test::Layout{});
    REQUIRE(p != nullptr);
    auto& undo = p->paramUndo();
    auto* drive = p->parameters().getParameter("drive");
    const float start = drive->getValue();
    CHECK_FALSE(undo.canUndo());

    drive->setValueNotifyingHost(0.2f);
    undo.recordChange("drive", start, 0.2f, 1000.0);
    drive->setValueNotifyingHost(0.3f);
    undo.recordChange("drive", 0.2f, 0.3f, 1200.0); // within 500 ms: the same step
    drive->setValueNotifyingHost(0.5f);
    undo.recordChange("drive", 0.3f, 0.5f, 2000.0); // later: a step of its own
    REQUIRE(undo.undo());
    CHECK(drive->getValue() == Catch::Approx(0.3f));
    REQUIRE(undo.undo());
    CHECK(drive->getValue() == Catch::Approx(start));
    CHECK_FALSE(undo.canUndo());
    REQUIRE(undo.redo());
    CHECK(drive->getValue() == Catch::Approx(0.3f));

    for (int i = 0; i < 150; ++i)
        undo.recordChange(i % 2 == 0 ? "shape" : "drive", 0.0f, 0.1f, 10000.0 + 1000.0 * i);
    int steps = 0;
    while (undo.undo())
        ++steps;
    CHECK(steps >= 100);
}

TEST_CASE("The editor opens at its size, scales, and paints every page", "[ui][editor]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto p = subgen::test::makeProcessor(subgen::test::Layout{});
    REQUIRE(p != nullptr);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor{p->createEditor()};
        REQUIRE(dynamic_cast<ui::SubGenEditor*>(editor.get()) != nullptr);
        auto& e = dynamic_cast<ui::SubGenEditor&>(*editor);
        CHECK(e.getWidth() == ui::kEditorWidth);
        CHECK(e.getHeight() == ui::kEditorHeight);
        CHECK(p->uiFeed().editorOpen());

        e.setUiScale(1.5);
        CHECK(e.getWidth() == 1500);
        CHECK(e.getHeight() == 930);
        CHECK(p->extraState().uiScale == Catch::Approx(1.5));
        e.setUiScale(5.0); // clamped to 200 %
        CHECK(e.getWidth() == 2000);
        e.setUiScale(1.0);

        const auto x = sine(55.0, 0.5, 48000.0, 20000);
        subgen::test::render(*p, {x, x});
        e.pollFeedForTest();
        for (int page = -1; page < static_cast<int>(ui::advancedPages().size()); ++page)
        {
            e.showAdvancedPage(page, false);
            CHECK(e.advancedPage() == page);
            const auto image = e.createComponentSnapshot(e.getLocalBounds());
            CHECK(image.getWidth() == ui::kEditorWidth);
        }
        for (int engine = 0; engine < 5; ++engine) // the engine page rebuilds for each engine
        {
            subgen::test::setParam(*p, "engine", static_cast<float>(engine));
            e.showAdvancedPage(1, false);
            (void)e.createComponentSnapshot(e.getLocalBounds());
        }
    }
    CHECK_FALSE(p->uiFeed().editorOpen());
}

// Pictures of the editor for a person to look at: SUBGEN_UI_SNAPSHOT_DIR=<dir> subgen_plugin_tests "[.snapshot]"
TEST_CASE("Editor snapshots", "[.snapshot]")
{
    const char* dir = std::getenv("SUBGEN_UI_SNAPSHOT_DIR");
    REQUIRE(dir != nullptr);
    const juce::ScopedJuceInitialiser_GUI gui;
    auto p = subgen::test::makeProcessor(subgen::test::Layout{}, 48000.0, 512, [](auto& proc) {
        subgen::test::setParam(proc, "drive", 46.0f);
        subgen::test::setParam(proc, "drive_mode", 1.0f);
        subgen::test::setParam(proc, "sub_level", -3.0f);
        subgen::test::setParam(proc, "boost_gain", 4.0f);
        subgen::test::setParam(proc, "sub2_level", -12.0f);
        subgen::test::setParam(proc, "eq2_on", 1.0f);
        subgen::test::setParam(proc, "eq2_gain", 4.5f);
        subgen::test::setParam(proc, "eq4_on", 1.0f);
        subgen::test::setParam(proc, "eq4_gain", -3.0f);
    });
    REQUIRE(p != nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> editor{p->createEditor()};
    auto& e = dynamic_cast<ui::SubGenEditor&>(*editor);
    // a kick-like source: 98 Hz with a falling envelope, repeated
    std::vector<double> x(48000 * 3);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double>(i % 24000) / 48000.0;
        x[i] = 0.6 * std::exp(-t * 6.0) * std::sin(2.0 * kPi * 98.0 * t) + 0.05 * std::sin(2.0 * kPi * 160.0 * t);
    }
    for (size_t block = 0; block < x.size(); block += 4096)
    {
        const auto len = std::min<size_t>(4096, x.size() - block);
        std::vector<double> part(x.begin() + static_cast<long>(block), x.begin() + static_cast<long>(block + len));
        subgen::test::render(*p, {part, part});
        e.pollFeedForTest();
    }
    const juce::File out(dir);
    out.createDirectory();
    const std::vector<std::pair<int, const char*>> shots{{-1, "main"}, {1, "engine"}, {2, "dynamics"}, {3, "eq"}, {5, "output"}};
    for (const auto& [page, name] : shots)
    {
        e.showAdvancedPage(page, false);
        const auto image = e.createComponentSnapshot(e.getLocalBounds(), true, 2.0f);
        juce::FileOutputStream stream(out.getChildFile(juce::String("ui_") + name + ".png"));
        stream.setPosition(0);
        stream.truncate();
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, stream));
    }
}
