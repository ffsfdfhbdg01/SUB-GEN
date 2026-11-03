// SPDX-License-Identifier: AGPL-3.0-or-later
// The sub chain (SPEC §7): the stages in their fixed order, the latency as the sum of the stages', Drive before or
// after the compressor with a crossfade, the NaN/Inf guard (T-RT-03), the LFE low-pass after Drive (T-DRV-06), the
// sub after chain A for Auto Align.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "chain/SubChain.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "dsp/FirDesign.h"
#include "support/AllocGuard.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::chain;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

// every stage that has a neutral state in it: level 0 dB, Boost 0 dB, Gate, compressor, EQ and limiter off, drive 0
SubChainParams neutral()
{
    SubChainParams p;
    p.levelGain = 1.0;
    p.boost.gainDb = 0.0;
    p.gate.on = false;
    p.drive.drivePct = 0.0;
    p.comp.on = false;
    p.limiter.on = false;
    return p;
}

template <typename Stage>
std::vector<double> run(Stage& stage, std::vector<double> x, size_t block = 64)
{
    for (size_t i = 0; i < x.size(); i += block)
        stage.process(std::span<double>(x.data() + i, std::min(block, x.size() - i)));
    return x;
}

std::vector<double> slice(const std::vector<double>& y, size_t from, size_t to)
{
    REQUIRE(from <= to);
    REQUIRE(to <= y.size()); // a window past the end would read beyond the signal
    return {y.begin() + static_cast<std::ptrdiff_t>(from), y.begin() + static_cast<std::ptrdiff_t>(to)};
}

double rmsDb(const std::vector<double>& y, size_t from, size_t to)
{
    return 20.0 * std::log10(test::rms(std::span<const double>(y.data() + from, to - from)));
}
} // namespace

TEST_CASE("neutral chain (every stage off or 0 dB) is a bit-exact delay", "[subchain]")
{
    // the filters and the phase stage have no neutral state (SPEC §7.6, §7.7: theta = 0 is chain A): the neutral chain
    // is exactly them, after a delay of latencySamples()
    const auto x = sig::bass808(kFs);
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        INFO("quality " << static_cast<int>(q));
        SubChain chain;
        chain.prepare(kFs, 64, q);
        chain.setParams(neutral());
        const auto y = run(chain, x);
        SubFilters filters;
        filters.prepare(kFs, 64, q);
        filters.setParams(neutral().filters);
        PhaseTool phase;
        phase.prepare(kFs);
        phase.setParams(neutral().phase);
        const auto reference = run(phase, run(filters, x));
        const auto latency = static_cast<size_t>(chain.latencySamples());
        bool exact = true;
        for (size_t n = 0; n < y.size(); ++n)
            exact = exact && y[n] == (n < latency ? 0.0 : reference[n - latency]);
        CHECK(exact);
    }
}

TEST_CASE("latency is the sum of the stage latencies: Live 0, Mix L_os + 2 ms, Print L_os + 5 + 5 ms (+ linear phase)",
          "[subchain]")
{
    for (const double fs : {44100.0, 48000.0, 96000.0})
    {
        INFO("fs " << fs);
        auto latency = [&](Quality q, const SubChainParams& p) {
            SubChain chain;
            chain.prepare(fs, 64, q);
            chain.setParams(p);
            return chain.latencySamples();
        };
        Drive mix, print;
        mix.prepare(fs, 64, Quality::Mix);
        print.prepare(fs, 64, Quality::Print);
        auto busy = SubChainParams{}; // drive, compressor, gate, limiter on: the latency does not move
        busy.drive.drivePct = 60.0;
        busy.comp.on = true;
        busy.gate.on = true;
        busy.drivePostComp = true;
        auto linear = neutral();
        linear.filters.linearPhase = true;
        for (const auto& p : {neutral(), busy})
        {
            CHECK(latency(Quality::Live, p) == 0);
            CHECK(latency(Quality::Mix, p) == mix.latencySamples() + msToSamples(2.0, fs));
            CHECK(latency(Quality::Print, p) == print.latencySamples() + 2 * msToSamples(5.0, fs));
        }
        CHECK(latency(Quality::Print, linear) ==
              print.latencySamples() + 2 * msToSamples(5.0, fs) + (dsp::linearPhaseTaps(fs) - 1) / 2 + 256);
        CHECK(latency(Quality::Mix, linear) == mix.latencySamples() + msToSamples(2.0, fs)); // Linear Phase: Print only
    }
}

TEST_CASE("T-RT-03 NaN/Inf injections: finite output, states reset, recovery within 1 s", "[subchain]")
{
    const auto x = sig::sine(55.0, kFs, 2.5, -12.0);
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()})
        for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
        {
            INFO("value " << bad << ", quality " << static_cast<int>(q));
            auto p = SubChainParams{};
            p.drive.drivePct = 40.0;
            p.comp.on = true;
            p.boost.gainDb = 6.0;
            auto render = [&](const std::vector<double>& in, bool& caught) {
                SubChain chain;
                chain.prepare(kFs, 64, q);
                chain.setParams(p);
                std::vector<double> y(in);
                caught = false;
                for (size_t i = 0; i < y.size(); i += 64)
                {
                    chain.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
                    caught = caught || chain.nanEvent();
                }
                return y;
            };
            auto injected = x;
            for (size_t i = 24000; i < 24010; ++i)
                injected[i] = bad;
            bool caught = false, never = false;
            const auto y = render(injected, caught);
            const auto reference = render(x, never);
            CHECK(std::all_of(y.begin(), y.end(), [](double v) { return std::isfinite(v); }));
            CHECK(caught);
            CHECK_FALSE(never);
            // 1 s after the injection the sub is back at its level
            CHECK(std::abs(rmsDb(y, 72000, 96000) - rmsDb(reference, 72000, 96000)) <= 0.5);
        }
}

TEST_CASE("T-DRV-06 LFE mode: Drive harmonics above lfe_lp attenuated by the slope +- 1 dB", "[subchain]")
{
    const auto x = sig::sine(55.0, kFs, 2.0, -12.0);
    for (const int slope : {24, 48})
    {
        INFO("lfe_lp_slope " << slope);
        auto p = neutral();
        p.drive.drivePct = 60.0;
        p.drive.mode = DriveMode::Hard;
        p.filters.lfeMode = true;
        p.filters.lfeHpHz = 20.0;
        p.filters.lfeLpHz = 120.0;
        p.filters.lfeLpSlope = slope;
        SubChain chain;
        chain.prepare(kFs, 64, Quality::Mix);
        chain.setParams(p);
        const auto y = run(chain, x);
        Drive drive;
        drive.prepare(kFs, 64, Quality::Mix);
        drive.setParams(p.drive);
        const auto d = run(drive, x);
        const auto chainAmps = test::harmonicAmps(std::span<const double>(y).last(48000), kFs, 55.0, 10);
        const auto driveAmps = test::harmonicAmps(std::span<const double>(d).last(48000), kFs, 55.0, 10);
        dsp::FilterCascade hp, lp;
        hp.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::HighPass, 4, 20.0, kFs));
        lp.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::LowPass, slope / 6, 120.0, kFs));
        int checked = 0;
        for (size_t k = 0; k < chainAmps.size(); ++k)
        {
            const double f = 55.0 * static_cast<double>(k + 1);
            const double filterDb = 20.0 * std::log10(std::abs(hp.response(f, kFs) * lp.response(f, kFs)));
            const double driveDb = 20.0 * std::log10(driveAmps[k]);
            if (f <= 120.0 || driveDb + filterDb < -90.0) // above lfe_lp and above the alias floor
                continue;
            INFO(f << " Hz: drive " << driveDb << " dB, filter " << filterDb << " dB, chain "
                   << 20.0 * std::log10(chainAmps[k]) << " dB");
            CHECK(std::abs(20.0 * std::log10(chainAmps[k]) - driveDb - filterDb) <= 1.0);
            ++checked;
        }
        CHECK(checked >= 2);
    }
}

TEST_CASE("Drive Pre/Post-Comp switch crossfades without clicks", "[subchain]")
{
    // The low-pass after the dynamics turns a hard switch into a few-ms thump, so the test reads the transition against
    // the steady Pre- and Post-Comp chains: right after the switch the output is still the Pre-Comp one (the 20 ms
    // crossfade has barely begun; a hard switch: -31 dB), and the incoming pair, warmed up on 50 ms of history, soon
    // matches the Post-Comp chain (a cold start: -23 dB at 100-200 ms with this slow compressor).
    const auto x = sig::sine(55.0, kFs, 1.2, -12.0); // the switch at 0.5 s, read until 0.5 s after it
    auto p = SubChainParams{};
    p.drive.drivePct = 50.0;
    p.comp.on = true;
    p.comp.threshDb = -30.0;
    p.comp.attackMs = 60.0;
    p.comp.releaseMs = 400.0;
    p.comp.autoRelease = false;
    constexpr size_t kSwitch = 24064;
    auto render = [&](int mode) { // 0: Pre, switched to Post at kSwitch; 1: always Pre; 2: always Post
        SubChain chain;
        chain.prepare(kFs, 64, Quality::Mix);
        auto q = p;
        q.drivePostComp = mode == 2;
        chain.setParams(q);
        std::vector<double> y(x);
        for (size_t i = 0; i < y.size(); i += 64)
        {
            if (mode == 0 && i == kSwitch)
            {
                q.drivePostComp = true;
                chain.setParams(q);
            }
            chain.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
        }
        return y;
    };
    const auto y = render(0), pre = render(1), post = render(2);
    auto nullTo = [&](const std::vector<double>& reference, double fromMs, double toMs) {
        const size_t a = kSwitch + static_cast<size_t>(fromMs * 48.0), b = kSwitch + static_cast<size_t>(toMs * 48.0);
        return test::nullDb(slice(reference, a, b), slice(y, a, b));
    };
    INFO("to Pre at 2-6 ms " << nullTo(pre, 2.0, 6.0) << " dB, to Post at 100-200 ms " << nullTo(post, 100.0, 200.0)
                             << " dB, at 300-500 ms " << nullTo(post, 300.0, 500.0) << " / " << nullTo(pre, 300.0, 500.0));
    CHECK(nullTo(pre, 2.0, 6.0) <= -50.0);
    CHECK(nullTo(post, 100.0, 200.0) <= -27.0);
    CHECK(nullTo(post, 300.0, 500.0) <= nullTo(pre, 300.0, 500.0) - 10.0); // the switch took
}

TEST_CASE("the sub level moves over 20 ms", "[subchain]")
{
    // 1-4 ms after a level change from -6 to +6 dB the output is still close to the old level (an unsmoothed level:
    // -25 dB)
    const auto x = sig::sine(55.0, kFs, 1.0, -12.0);
    auto render = [&](bool change) {
        SubChain chain;
        chain.prepare(kFs, 64, Quality::Live);
        auto p = SubChainParams{};
        p.levelGain = 0.5;
        p.limiter.on = false;
        chain.setParams(p);
        std::vector<double> y(x);
        for (size_t i = 0; i < y.size(); i += 64)
        {
            if (change && i == 24064)
            {
                p.levelGain = 2.0;
                chain.setParams(p);
            }
            chain.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
        }
        return y;
    };
    const auto y = render(true), before = render(false);
    const double early = test::nullDb(slice(before, 24112, 24256), slice(y, 24112, 24256));
    INFO("1-4 ms after the change: " << early << " dB from the old level");
    CHECK(early <= -40.0);
    CHECK(std::abs(rmsDb(y, 36000, 48000) - rmsDb(before, 36000, 48000) - 20.0 * std::log10(4.0)) <= 0.05);
}

TEST_CASE("afterChainA gives the last block of the sub after chain A", "[subchain]")
{
    const auto x = sig::bass808(kFs);
    auto p = neutral();
    p.drive.drivePct = 30.0;
    SubChain plain;
    plain.prepare(kFs, 64, Quality::Live);
    plain.setParams(p); // theta 0, no polarity, limiter off in Live: the output is chain A
    p.phase = {true, 90.0};
    SubChain rotated;
    rotated.prepare(kFs, 64, Quality::Live);
    rotated.setParams(p);
    std::vector<double> a(x), b(x);
    bool same = true;
    for (size_t i = 0; i < x.size(); i += 64)
    {
        const size_t n = std::min<size_t>(64, x.size() - i);
        plain.process(std::span<double>(a.data() + i, n));
        rotated.process(std::span<double>(b.data() + i, n));
        const auto chainA = rotated.afterChainA();
        same = same && chainA.size() == n && std::equal(chainA.begin(), chainA.end(), a.begin() + static_cast<std::ptrdiff_t>(i));
    }
    CHECK(same);
}

TEST_CASE("offline Print renders wait for the Linear Phase kernel", "[subchain]")
{
    const auto x = sig::sine(55.0, kFs, 0.5, -12.0);
    auto p = neutral();
    p.filters.linearPhase = true;
    SubChain chain;
    chain.prepare(kFs, 64, Quality::Print);
    chain.setNonRealtime(true);
    chain.setParams(p);
    const auto y = run(chain, x); // far faster than real time: without the wait the kernel would come too late
    CHECK(rmsDb(y, 12000, 24000) > -30.0);
}

TEST_CASE("process does not allocate", "[subchain]")
{
    const auto x = sig::bass808(kFs);
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        SubChain chain;
        chain.prepare(kFs, 64, q);
        auto p = SubChainParams{};
        p.drive.drivePct = 40.0;
        p.comp.on = true;
        p.gate.on = true;
        p.filters.linearPhase = q == Quality::Print;
        chain.setParams(p);
        std::vector<double> y(x);
        size_t allocations = 0;
        for (size_t i = 0; i < y.size(); i += 64)
        {
            test::AllocGuard guard;
            if (i == 9600) // a Drive position switch, a family change and a NaN on the way
            {
                p.drivePostComp = true;
                p.filters.family = dsp::FilterFamily::Bessel;
                p.phase.rotationDeg = 45.0;
                chain.setParams(p);
                y[i + 3] = std::numeric_limits<double>::quiet_NaN();
            }
            chain.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
            allocations += guard.count();
        }
        CHECK(allocations == 0);
    }
}
