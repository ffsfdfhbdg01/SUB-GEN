// SPDX-License-Identifier: AGPL-3.0-or-later
// The engine host and HYBRID (SPEC §6.1, §6.6, T-HYB-01, T-ENG-01): one latency E for every engine, crossfaded
// switching with a warmed-up incoming engine, and HYBRID as the weighted sum of DIVIDE and HARMONIC.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>

#include "core/Quality.h"
#include "engines/Divide.h"
#include "engines/EngineHost.h"
#include "engines/Harmonic.h"
#include "engines/Hybrid.h"
#include "support/AllocGuard.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::engines;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

// The host over x in blocks of `block`; switch(i) is asked before the block that starts at sample i.
std::vector<double> runHost(EngineHost& host, const std::vector<double>& x, size_t block,
                            const std::function<void(size_t)>& before = {})
{
    std::vector<double> out(x.size());
    const std::vector<double> key(x.size(), 0.0);
    for (size_t i = 0; i < x.size(); i += block)
    {
        if (before)
            before(i);
        const size_t len = std::min(block, x.size() - i);
        host.process(std::span<const double>(x.data() + i, len), std::span<const double>(key.data() + i, len),
                     std::span<double>(out.data() + i, len));
    }
    return out;
}

std::vector<double> runEngine(Engine& engine, const std::vector<double>& x, int lookahead, const EngineControl& c)
{
    engine.prepare(kFs, 64, lookahead);
    engine.setControl(c);
    std::vector<double> out(x.size());
    const std::vector<double> key(x.size(), 0.0);
    for (size_t i = 0; i < x.size(); i += 64)
    {
        const size_t len = std::min<size_t>(64, x.size() - i);
        engine.process(std::span<const double>(x.data() + i, len), std::span<const double>(key.data() + i, len),
                       std::span<double>(out.data() + i, len));
    }
    return out;
}

double secondDifference(const std::vector<double>& y, size_t i)
{
    return std::abs(y[i] - 2.0 * y[i - 1] + y[i - 2]);
}
} // namespace

TEST_CASE("T-HYB-01 Hybrid equals the weighted sum of independent engines (null <= -120 dB)", "[engine-host]")
{
    const auto x = sig::bass808(kFs);
    const int look = engineLookaheadSamples(Quality::Mix, kFs);
    EngineControl c;
    c.hybDivide = 0.8;
    c.hybHarmonic = 0.35;
    c.sub2Gain = 0.5;
    Hybrid hybrid;
    Divide divide;
    Harmonic harmonic;
    const auto h = runEngine(hybrid, x, look, c);
    const auto d = runEngine(divide, x, look, c);
    const auto m = runEngine(harmonic, x, look, c);
    std::vector<double> sum(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        sum[i] = c.hybDivide * d[i] + c.hybHarmonic * m[i];
    CHECK(test::nullDb(sum, h) <= -120.0);
    double energy = 0.0;
    for (const double v : h)
        energy += v * v;
    CHECK(energy > 1.0); // it is not silent
}

TEST_CASE("T-ENG-01 switching every 0.5 s over all engines on SIG-04: no click (max 2nd difference in the crossfade <= "
          "the steady maximum + 1e-3), latency constant",
          "[engine-host]")
{
    const auto x = sig::bass808(kFs, {.sustain = 3.0});
    EngineHost host;
    host.prepare(kFs, 64, Quality::Mix);
    host.setControl(EngineControl{});
    const int latency = host.latencySamples();
    const EngineType order[] = {EngineType::Divide, EngineType::Track,  EngineType::Trigger,
                                EngineType::Harmonic, EngineType::Hybrid, EngineType::Divide};
    std::vector<size_t> switches;
    bool latencyConstant = true;
    const auto y = runHost(host, x, 64, [&](size_t i) {
        if (i > 0 && i % 24000 == 0 && i / 24000 < std::size(order))
        {
            host.setEngine(order[i / 24000]);
            switches.push_back(i);
        }
        latencyConstant = latencyConstant && host.latencySamples() == latency;
    });
    REQUIRE(switches.size() == 5);
    CHECK(latencyConstant);
    CHECK(latency == engineLookaheadSamples(Quality::Mix, kFs));
    const auto fade = static_cast<size_t>(0.030 * kFs);
    auto inFade = [&](size_t i) {
        return std::any_of(switches.begin(), switches.end(), [&](size_t s) { return i >= s && i < s + fade + 2; });
    };
    double steady = 0.0;
    double fading = 0.0;
    for (size_t i = 2; i < y.size(); ++i)
        (inFade(i) ? fading : steady) = std::max(inFade(i) ? fading : steady, secondDifference(y, i));
    INFO("steady " << steady << ", in the crossfades " << fading);
    CHECK(fading <= steady + 1e-3);
}

TEST_CASE("DIVIDE output is delayed by exactly E in Mix and Print", "[engine-host]")
{
    const auto x = sig::kick(kFs, {.nHits = 2, .spacing = 0.5, .levelDb = -6.0});
    for (const Quality q : {Quality::Mix, Quality::Print})
    {
        INFO((q == Quality::Mix ? "Mix" : "Print"));
        EngineHost host;
        host.prepare(kFs, 64, q);
        host.setControl(EngineControl{});
        const int look = engineLookaheadSamples(q, kFs);
        CHECK(host.latencySamples() == look);
        Divide divide;
        CHECK(runHost(host, x, 64) == runEngine(divide, x, look, EngineControl{}));
    }
}

TEST_CASE("TRACK switched in on a sustained note is confident within 4 ms of the switch", "[engine-host]")
{
    // the incoming engine is warmed up with the last 200 ms of the source, more than TRACK's 83.5 ms window
    const auto x = sig::sine(55.0, kFs, 2.0, -12.0);
    for (const Quality q : {Quality::Live, Quality::Mix})
    {
        INFO((q == Quality::Live ? "Live" : "Mix"));
        EngineHost host;
        host.prepare(kFs, 48, q);
        host.setControl(EngineControl{});
        const size_t at = 48000;
        size_t confidentAt = 0;
        runHost(host, x, 48, [&](size_t i) {
            if (i == at)
                host.setEngine(EngineType::Track);
            if (i > at && confidentAt == 0 && host.confidence() >= 0.5)
                confidentAt = i;
        });
        REQUIRE(confidentAt > 0);
        CHECK(confidentAt - at <= static_cast<size_t>(0.004 * kFs));
    }
}

TEST_CASE("re-prepare at a new sample rate clears every engine", "[engine-host]")
{
    for (const EngineType type : {EngineType::Divide, EngineType::Track, EngineType::Trigger, EngineType::Harmonic,
                                  EngineType::Hybrid})
    {
        INFO("engine " << static_cast<int>(type));
        EngineHost host;
        host.prepare(kFs, 64, Quality::Mix);
        host.setControl(EngineControl{});
        host.setEngine(type);
        runHost(host, sig::bass808(kFs), 64);
        host.prepare(96000.0, 64, Quality::Mix);
        CHECK(host.latencySamples() == engineLookaheadSamples(Quality::Mix, 96000.0));
        EngineHost fresh;
        fresh.prepare(96000.0, 64, Quality::Mix);
        fresh.setControl(EngineControl{});
        fresh.setEngine(type);
        const auto x = sig::bass808(96000.0);
        CHECK(runHost(host, x, 64) == runHost(fresh, x, 64));
    }
}

TEST_CASE("the host is silent before prepare and takes a usable sample rate from a broken one", "[engine-host]")
{
    // security review: an empty warm-up ring (a sample rate of 0 or NaN) divided by zero, and an unprepared host
    // looped forever on its empty scratch buffer
    const auto x = sig::sine(55.0, kFs, 0.1, -12.0);
    std::vector<double> out(x.size(), 1.0);
    const std::vector<double> key(x.size(), 0.0);
    EngineHost unprepared;
    unprepared.process(x, key, out);
    CHECK(std::all_of(out.begin(), out.end(), [](double v) { return v == 0.0; }));
    Hybrid hybrid; // the same chunk loop
    std::fill(out.begin(), out.end(), 1.0);
    hybrid.process(x, key, out);
    CHECK(std::all_of(out.begin(), out.end(), [](double v) { return v == 0.0; }));
    for (const double fs : {0.0, -48000.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        INFO("fs " << fs);
        EngineHost host;
        host.prepare(fs, 64, Quality::Mix);
        host.setControl(EngineControl{});
        CHECK(host.latencySamples() == engineLookaheadSamples(Quality::Mix, 48000.0));
        host.process(x, key, out);
        CHECK(std::all_of(out.begin(), out.end(), [](double v) { return std::isfinite(v); }));
    }
}

TEST_CASE("the host does not allocate, splits blocks larger than prepared and does not depend on the block size",
          "[engine-host]")
{
    const auto x = sig::bass808(kFs, {.sustain = 1.0});
    auto run = [&](size_t block) {
        EngineHost host;
        host.prepare(kFs, 64, Quality::Mix);
        host.setControl(EngineControl{});
        std::vector<double> out(x.size());
        const std::vector<double> key(x.size(), 0.0);
        size_t allocations = 0;
        for (size_t i = 0; i < x.size(); i += block)
        {
            if (i == 24000)
                host.setEngine(EngineType::Track); // the switch starts at the same sample for blocks dividing 24000
            const size_t len = std::min(block, x.size() - i);
            test::AllocGuard guard;
            host.process(std::span<const double>(x.data() + i, len), std::span<const double>(key.data() + i, len),
                         std::span<double>(out.data() + i, len));
            allocations += guard.count();
        }
        CHECK(allocations == 0);
        return out;
    };
    const auto a = run(1);
    CHECK(run(64) == a);
    CHECK(run(1000) == a); // larger than the prepared 64
}
