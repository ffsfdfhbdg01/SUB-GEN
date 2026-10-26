// SPDX-License-Identifier: AGPL-3.0-or-later
// Layouts and LFE in the processor (SPEC_IO_UI §1-4; T-IO-01..05, T-LFE-02..05): the dry stays bit-exact on every
// supported layout, the sub goes only where the output mode routes it, x.0 -> x.1 feeds the new LFE, the LFE ceiling and
// bleed, the LFE warnings.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "ProcessorHarness.h"
#include "plugin/Warnings.h"
#include "routing/ChannelMap.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::test;
using plugin::SubGenProcessor;
using plugin::Warning;
using Set = juce::AudioChannelSet;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

const std::vector<Set>& supportedLayouts()
{
    static const std::vector<Set> all{
        Set::mono(), Set::stereo(), Set::createLCR(), Set::createLRS(), Set::createLCRS(), Set::quadraphonic(),
        Set::create5point0(), Set::create5point1(), Set::create6point0(), Set::create6point1(), Set::create6point0Music(),
        Set::create6point1Music(), Set::create7point0(), Set::create7point1(), Set::create7point0SDDS(),
        Set::create7point1SDDS(), Set::create5point0point2(), Set::create5point1point2(), Set::create5point0point4(),
        Set::create5point1point4(), Set::create7point0point2(), Set::create7point1point2(), Set::create7point0point4(),
        Set::create7point1point4(), Set::create7point0point6(), Set::create7point1point6(), Set::create9point0point4(),
        Set::create9point1point4(), Set::create9point0point6(), Set::create9point1point6(), Set::create9point0point4ITU(),
        Set::create9point1point4ITU(), Set::create9point0point6ITU(), Set::create9point1point6ITU(), Set::pentagonal(),
        Set::hexagonal(), Set::octagonal()};
    return all;
}

// a host that applies any layout, as some do: the processor itself must notice an unusable one (W-12)
struct ForcedLayout : SubGenProcessor
{
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
};
} // namespace

TEST_CASE("T-IO-05 supported layouts, pairs and sidechains; ambisonic rejected", "[layouts]")
{
    SubGenProcessor p;
    for (const auto& set : supportedLayouts())
    {
        INFO(set.getDescription());
        CHECK(p.checkBusesLayoutSupported(busesOf({set, set})));
    }
    CHECK(p.checkBusesLayoutSupported(busesOf({Set::create5point0(), Set::create5point1()})));
    CHECK(p.checkBusesLayoutSupported(busesOf({Set::create7point0point4(), Set::create7point1point4()})));
    CHECK(p.checkBusesLayoutSupported(busesOf({Set::mono(), Set::create7point1()})));
    CHECK(p.checkBusesLayoutSupported(busesOf({Set::stereo(), Set::stereo(), Set::mono()})));
    CHECK(p.checkBusesLayoutSupported(busesOf({Set::stereo(), Set::stereo(), Set::stereo()})));
    CHECK_FALSE(p.checkBusesLayoutSupported(busesOf({Set::stereo(), Set::stereo(), Set::create5point1()})));
    CHECK_FALSE(p.checkBusesLayoutSupported(busesOf({Set::ambisonic(1), Set::ambisonic(1)})));
    CHECK_FALSE(p.checkBusesLayoutSupported(busesOf({Set::stereo(), Set::create5point1()})));
}

TEST_CASE("T-IO-01 SIG-15 on every supported layout: dry bit-exact, sub only where routed", "[layouts]")
{
    for (const auto& set : supportedLayouts())
    {
        INFO(set.getDescription());
        const auto in = sig15(set, kFs, 0.6);
        auto off = makeProcessor({set, set}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "sub_level", -60.0f); });
        REQUIRE(off != nullptr);
        const auto dry = render(*off, in);
        const auto latency = static_cast<size_t>(off->getLatencySamples());
        bool exact = true;
        for (size_t c = 0; c < dry.size(); ++c)
            exact = exact && added(dry[c], in[c], latency) == std::vector<double>(dry[c].size(), 0.0);
        CHECK(exact);
        // with the sub: Auto is LFE with an LFE channel (the sub in LFE alone), else Mono (Front and Center)
        auto on = makeProcessor({set, set}, kFs, 512);
        const auto out = render(*on, in);
        const bool lfe = set.getChannelTypes().contains(Set::LFE);
        for (int c = 0; c < set.size(); ++c)
        {
            const auto cls = routing::classify(set.getTypeOfChannel(c)).cls;
            const bool routed = lfe ? cls == routing::ChannelClass::Lfe
                                    : (cls == routing::ChannelClass::Front || cls == routing::ChannelClass::Center);
            const auto sub = added(out[static_cast<size_t>(c)], in[static_cast<size_t>(c)], latency);
            INFO("channel " << c);
            if (routed)
                CHECK(rmsDb(sub, sub.size() / 2, sub.size()) > -60.0);
            else
                CHECK(rmsDb(sub, 0, sub.size()) <= -120.0); // T-IO-04: no leakage
        }
    }
}

TEST_CASE("T-IO-03 x.0 -> x.1 pairs: the LFE carries the sub", "[layouts]")
{
    for (const auto& [in, out] : {std::pair{Set::create5point0(), Set::create5point1()}, std::pair{Set::create7point0(), Set::create7point1()},
                                  std::pair{Set::create7point0point4(), Set::create7point1point4()},
                                  std::pair{Set::create9point0point6(), Set::create9point1point6()}})
    {
        INFO(out.getDescription());
        auto p = makeProcessor({in, out}, kFs, 512);
        REQUIRE(p != nullptr);
        const auto x = sig15(in, kFs, 0.6);
        const auto y = render(*p, x);
        const auto latency = static_cast<size_t>(p->getLatencySamples());
        for (int c = 0; c < out.size(); ++c)
        {
            const auto type = out.getTypeOfChannel(c);
            const auto& ch = y[static_cast<size_t>(c)];
            if (type == Set::LFE)
            {
                CHECK(rmsDb(ch, ch.size() / 2, ch.size()) > -60.0); // the sub, no dry
                continue;
            }
            const auto& source = x[static_cast<size_t>(in.getChannelIndexForType(type))];
            CHECK(rmsDb(added(ch, source, latency), 0, ch.size()) <= -120.0); // the dry alone
        }
    }
}

TEST_CASE("T-IO-04 leakage into unrouted channels <= -120 dB", "[layouts]")
{
    const auto set = Set::create7point1point4();
    auto p = makeProcessor({set, set}, kFs, 512, [](SubGenProcessor& s) {
        setParam(s, "out_mode", 3.0f);        // Surround
        setParam(s, "route_height", -60.0f);  // Off
        setParam(s, "route_surround", -60.0f);
    });
    const auto in = sig15(set, kFs, 0.6);
    const auto out = render(*p, in);
    const auto latency = static_cast<size_t>(p->getLatencySamples());
    for (int c = 0; c < set.size(); ++c)
    {
        const auto cls = routing::classify(set.getTypeOfChannel(c)).cls;
        if (cls != routing::ChannelClass::Height && cls != routing::ChannelClass::Surround)
            continue;
        INFO("channel " << c);
        CHECK(rmsDb(added(out[static_cast<size_t>(c)], in[static_cast<size_t>(c)], latency), 0, in[0].size()) <= -120.0);
    }
}

TEST_CASE("T-LFE-02 LFE mode: only the LFE channel carries sub; Wet Only leakage <= -120 dB", "[layouts]")
{
    const auto set = Set::create5point1();
    auto p = makeProcessor({set, set}, kFs, 512, [](SubGenProcessor& s) {
        setParam(s, "out_mode", 4.0f);
        setParam(s, "wet_only", 1.0f);
    });
    const auto in = sig15(set, kFs, 0.6);
    const auto out = render(*p, in);
    for (int c = 0; c < set.size(); ++c)
    {
        const auto& ch = out[static_cast<size_t>(c)];
        INFO("channel " << c);
        if (set.getTypeOfChannel(c) == Set::LFE)
            CHECK(rmsDb(ch, ch.size() / 2, ch.size()) > -60.0);
        else
            CHECK(rmsDb(ch, 4800, ch.size()) <= -120.0); // after the 10 ms Wet Only ramp
    }
}

TEST_CASE("T-LFE-03 the LFE ceiling holds on stress signals", "[layouts]")
{
    const auto set = Set::create5point1();
    for (const int q : {0, 1, 2})
        for (const auto& stress : {sig::square(40.0, kFs, 1.0, 0.0), sig::dc(kFs, 1.0, 0.9)})
        {
            INFO("quality " << q);
            auto p = makeProcessor({set, set}, kFs, 512, [&](SubGenProcessor& s) {
                setParam(s, "quality", static_cast<float>(q));
                setParam(s, "lfe_ceiling", -6.0f);
                setParam(s, "sub_level", 18.0f);
                setParam(s, "wet_only", 1.0f);
            });
            Channels in(static_cast<size_t>(set.size()), stress);
            in[static_cast<size_t>(set.getChannelIndexForType(Set::LFE))].assign(stress.size(), 0.0);
            const auto out = render(*p, in);
            double peak = 0.0;
            for (const double v : out[static_cast<size_t>(set.getChannelIndexForType(Set::LFE))])
                peak = std::max(peak, std::abs(v));
            CHECK(peak <= std::pow(10.0, -6.0 / 20.0) + 1e-9);
        }
}

TEST_CASE("T-LFE-04 bleed level +- 0.1 dB, band <= 120 Hz", "[layouts]")
{
    const auto set = Set::create5point1();
    auto p = makeProcessor({set, set}, kFs, 512, [](SubGenProcessor& s) {
        setParam(s, "out_mode", 4.0f);
        setParam(s, "lfe_bleed", -20.0f);
        setParam(s, "wet_only", 1.0f);
    });
    const auto in = Channels(static_cast<size_t>(set.size()), sig::sine(80.0, kFs, 1.0, -12.0));
    const auto out = render(*p, in);
    const auto& lfe = out[static_cast<size_t>(set.getChannelIndexForType(Set::LFE))];
    const auto& left = out[static_cast<size_t>(set.getChannelIndexForType(Set::left))];
    const auto& centre = out[static_cast<size_t>(set.getChannelIndexForType(Set::centre))];
    CHECK(std::abs(rmsDb(left, 24000, 48000) - rmsDb(lfe, 24000, 48000) + 20.0) <= 0.1);
    CHECK(std::abs(rmsDb(centre, 24000, 48000) - rmsDb(lfe, 24000, 48000) + 20.0) <= 0.1);
    CHECK(rmsDb(out[static_cast<size_t>(set.getChannelIndexForType(Set::leftSurround))], 0, 48000) <= -120.0);
    // the bleed is the LFE signal: nothing above the LFE band but its filter's tail
    CHECK(test::bandDb(std::span<const double>(left).subspan(24000), kFs, 200.0, 20000.0)
          <= test::bandDb(std::span<const double>(left).subspan(24000), kFs, 20.0, 120.0) - 30.0);
}

TEST_CASE("T-LFE-05 W-01 and W-05 conditions", "[layouts]")
{
    // W-01: LFE mode on a layout without LFE behaves as Mono
    auto stereo = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "out_mode", 4.0f); });
    const auto bass = Channels(2, sig::sine(60.0, kFs, 0.6, -12.0));
    const auto y = render(*stereo, bass);
    CHECK(stereo->warnings().has(Warning::NoLfeChannel));
    CHECK(rmsDb(added(y[0], bass[0], static_cast<size_t>(stereo->getLatencySamples())), 14400, 28800) > -60.0);
    // W-05: LFE mode, bleed Off, the LFE sub louder than the Front channels' bass: lost in a stereo downmix
    const auto set = Set::create5point1();
    auto quietFront = sig15(set, kFs, 0.6);
    for (int c = 0; c < set.size(); ++c)
        if (set.getTypeOfChannel(c) != Set::centre)
            for (auto& v : quietFront[static_cast<size_t>(c)])
                v *= 0.01; // the bass sits in the centre: the Front pair is quiet in 20-120 Hz
    auto lost = makeProcessor({set, set}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "out_mode", 4.0f); });
    render(*lost, quietFront);
    CHECK(lost->warnings().has(Warning::LfeOnlyContent));
    CHECK_FALSE(lost->warnings().has(Warning::NoLfeChannel)); // LFE mode on a layout with LFE
    auto bled = makeProcessor({set, set}, kFs, 512, [](SubGenProcessor& s) {
        setParam(s, "out_mode", 4.0f);
        setParam(s, "lfe_bleed", -12.0f);
    });
    render(*bled, quietFront);
    CHECK_FALSE(bled->warnings().has(Warning::LfeOnlyContent));
}

TEST_CASE("an unusable layout forced on the processor passes audio through with W-12", "[layouts]")
{
    auto p = makeProcessor<ForcedLayout>({Set::ambisonic(1), Set::ambisonic(1)}, kFs, 512);
    REQUIRE(p != nullptr);
    CHECK(p->warnings().has(Warning::LayoutNotSupported));
    CHECK(p->getLatencySamples() == 0);
    const auto in = Channels(4, sig::sine(60.0, kFs, 0.2, -12.0));
    CHECK(render(*p, in) == in);
}

TEST_CASE("out_mode LFE without an LFE channel behaves as Mono with W-01; sc_use Detect without the sidechain bus uses "
          "main with W-02; the mono layout works",
          "[layouts]")
{
    const auto bass = sig::sine(60.0, kFs, 0.6, -12.0);
    auto lfe = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "out_mode", 4.0f); });
    auto mono = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "out_mode", 1.0f); });
    const auto a = render(*lfe, Channels(2, bass));
    const auto b = render(*mono, Channels(2, bass));
    CHECK(lfe->warnings().has(Warning::NoLfeChannel));
    CHECK(a == b);
    auto detect = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "sc_use", 1.0f); });
    auto plain = makeProcessor({}, kFs, 512);
    const auto c = render(*detect, Channels(2, bass));
    CHECK(detect->warnings().has(Warning::KeyNotConnected));
    CHECK(c == render(*plain, Channels(2, bass)));
    auto m = makeProcessor({Set::mono(), Set::mono()}, kFs, 512);
    REQUIRE(m != nullptr);
    const auto y = render(*m, Channels(1, bass));
    const auto sub = added(y[0], bass, static_cast<size_t>(m->getLatencySamples()));
    CHECK(rmsDb(sub, 14400, 28800) > -60.0); // the sub in the mono channel (Center)
}

TEST_CASE("mono into 5.1 (SHOULD): the dry only in the Center channel, bit-exact", "[layouts]")
{
    const auto set = Set::create5point1();
    auto p = makeProcessor({Set::mono(), set}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "sub_level", -60.0f); });
    REQUIRE(p != nullptr);
    const auto x = sig15(Set::mono(), kFs, 0.25)[0];
    const auto y = render(*p, Channels(1, x));
    const auto latency = static_cast<size_t>(p->getLatencySamples());
    for (int c = 0; c < set.size(); ++c)
    {
        INFO("channel " << c);
        const bool centre = set.getTypeOfChannel(c) == Set::centre;
        bool exact = true;
        for (size_t i = 0; i < x.size(); ++i)
            exact = exact && y[static_cast<size_t>(c)][i] == (centre && i >= latency ? x[i - latency] : 0.0);
        CHECK(exact);
    }
}
