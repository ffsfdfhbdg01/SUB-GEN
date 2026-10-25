// SPDX-License-Identifier: AGPL-3.0-or-later
// The sub path's limiter (SPEC §7.8, T-LIM-01/02): Live c tanh(x / c) with ADAA; Mix and Print a lookahead limiter
// whose ceiling holds by construction; Print's true-peak mode on a 4x interpolation; against the prototype
// (tests/golden/limiter.sgg, proto/subgen_proto/dynamics.limiter) and the TEST_PLAN criteria.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "chain/Limiter.h"
#include "core/Quality.h"
#include "support/AllocGuard.h"
#include "support/Golden.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::chain;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;
const double kCeiling = std::pow(10.0, -1.0 / 20.0);

LimiterParams params(double ceilingDb = -1.0, bool truePeak = false)
{
    LimiterParams p;
    p.on = true;
    p.ceilingDb = ceilingDb;
    p.releaseMs = 100.0;
    p.truePeak = truePeak;
    return p;
}

struct Run
{
    std::vector<double> out;  // raw, delayed by the latency
    std::vector<double> gain; // per output sample
    int latency = 0;
};

Run runLimiter(const std::vector<double>& x, Quality q, const LimiterParams& p, size_t block = 1)
{
    Limiter lim;
    lim.prepare(kFs, static_cast<int>(block), q);
    lim.setParams(p);
    Run r;
    r.latency = lim.latencySamples();
    r.out = x;
    for (size_t i = 0; i < x.size(); i += block)
    {
        lim.process(std::span<double>(r.out.data() + i, std::min(block, x.size() - i)));
        if (block == 1)
            r.gain.push_back(lim.gain());
    }
    return r;
}

std::vector<double> concat(std::initializer_list<std::vector<double>> parts)
{
    std::vector<double> out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

std::vector<std::vector<double>> stressSignals()
{
    // a square 6 dB over the ceiling, kicks 9 dB over, noise at +6 dBFS, a 55 Hz sine at +12 dBFS jumping in level
    auto noise = sig::xorshiftNoise(48000, 9);
    for (auto& v : noise)
        v *= 2.0;
    auto jumps = sig::sine(55.0, kFs, 1.0, 12.0);
    for (size_t i = 0; i < jumps.size(); ++i)
        jumps[i] *= (i / 6000) % 2 == 0 ? 1.0 : 0.05;
    return {sig::square(40.0, kFs, 1.0, -1.0 + 6.0), sig::kick(kFs, {.nHits = 2, .spacing = 0.3, .levelDb = -1.0 + 9.0}),
            noise, jumps};
}

double peakOf(const std::vector<double>& y)
{
    double p = 0.0;
    for (const double v : y)
        p = std::max(p, std::abs(v));
    return p;
}
} // namespace

TEST_CASE("T-LIM-01 four stress signals x Live/2 ms/5 ms: |y| <= ceiling + 1e-9", "[limiter]")
{
    for (const auto& x : stressSignals())
        for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
        {
            INFO("quality " << static_cast<int>(q));
            CHECK(peakOf(runLimiter(x, q, params()).out) <= kCeiling + 1e-9);
            CHECK(peakOf(runLimiter(x, q, params(), 512).out) <= kCeiling + 1e-9);
        }
}

TEST_CASE("T-LIM-01 true peak (Print, lim_tp): 4x-oversampled peak <= ceiling + 0.01 dB with the same interpolator",
          "[limiter]")
{
    // inter-sample peaks: a sine at fs / 4 whose 45 degree phase puts every crest between samples (3 dB above them), and the stress set
    auto signals = stressSignals();
    signals.push_back(sig::sine(12000.0, kFs, 0.5, 3.0, std::numbers::pi / 4.0));
    for (const auto& x : signals)
    {
        const auto y = runLimiter(x, Quality::Print, params(-1.0, true)).out;
        TruePeakInterpolator measure;
        double peak = 0.0;
        for (const double v : y)
        {
            const auto phases = measure.process(v);
            peak = std::max({peak, std::abs(v), std::abs(phases[0]), std::abs(phases[1]), std::abs(phases[2]),
                             std::abs(phases[3])});
        }
        INFO("true peak " << 20.0 * std::log10(peak) << " dBFS");
        CHECK(20.0 * std::log10(peak / kCeiling) <= 0.01);
    }
    // the sample-peak mode lets the inter-sample crests of the 12 kHz sine through
    const auto x = sig::sine(12000.0, kFs, 0.5, 3.0, std::numbers::pi / 4.0);
    const auto y = runLimiter(x, Quality::Print, params(-1.0, false)).out;
    TruePeakInterpolator measure;
    double peak = 0.0;
    for (const double v : y)
        for (const double p : measure.process(v))
            peak = std::max(peak, std::abs(p));
    CHECK(20.0 * std::log10(peak / kCeiling) > 1.0);
}

TEST_CASE("T-LIM-02 40 Hz, 6 dB over, release 100 ms: THD Mix <= -35 dB, Print <= -50 dB", "[limiter]")
{
    const auto x = sig::sine(40.0, kFs, 3.0, -1.0 + 6.0);
    for (const auto& [q, bound] : {std::pair{Quality::Mix, -35.0}, std::pair{Quality::Print, -50.0}})
    {
        const auto r = runLimiter(x, q, params(), 512);
        const double thd = test::thdDb(std::span<const double>(r.out.data() + 48000, 48000), kFs, 40.0);
        INFO("quality " << static_cast<int>(q) << ": THD " << thd << " dB");
        CHECK(thd <= bound);
    }
    const auto live = runLimiter(x, Quality::Live, params(), 512);
    UNSCOPED_INFO("Live THD (informational, P2 -15.2 dB): "
                  << test::thdDb(std::span<const double>(live.out.data() + 48000, 48000), kFs, 40.0) << " dB");
    CHECK(std::isfinite(test::thdDb(std::span<const double>(live.out.data() + 48000, 48000), kFs, 40.0)));
}

TEST_CASE("latency is exactly round(lookahead fs)", "[limiter]")
{
    for (const double fs : {44100.0, 48000.0, 96000.0})
    {
        Limiter lim;
        lim.prepare(fs, 64, Quality::Live);
        CHECK(lim.latencySamples() == 0);
        lim.prepare(fs, 64, Quality::Mix);
        CHECK(lim.latencySamples() == static_cast<int>(std::nearbyint(0.002 * fs)));
        lim.prepare(fs, 64, Quality::Print);
        CHECK(lim.latencySamples() == static_cast<int>(std::nearbyint(0.005 * fs)));
    }
    // an impulse below the ceiling comes out D samples later, alone
    for (const Quality q : {Quality::Mix, Quality::Print})
    {
        const auto r = runLimiter(sig::impulse(4800, 0.5), q, params());
        for (size_t i = 0; i < r.out.size(); ++i)
            if (i == static_cast<size_t>(r.latency))
                CHECK(std::abs(r.out[i] - 0.5) <= 1e-12);
            else
                CHECK(r.out[i] == 0.0);
    }
}

TEST_CASE("a 40 Hz sine below the ceiling passes unchanged from its first sample after reset (within 1e-15, delayed by "
          "D)",
          "[limiter]")
{
    const auto x = sig::sine(40.0, kFs, 1.0, -6.0);
    for (const Quality q : {Quality::Mix, Quality::Print})
        for (const bool tp : {false, true})
        {
            INFO("quality " << static_cast<int>(q) << " true peak " << tp);
            const auto r = runLimiter(x, q, params(-1.0, tp && q == Quality::Print));
            const auto d = static_cast<size_t>(r.latency);
            double worst = 0.0;
            for (size_t i = d; i < x.size(); ++i)
                worst = std::max(worst, std::abs(r.out[i] - x[i - d]));
            CHECK(worst <= 1e-15);
        }
}

TEST_CASE("off is bit-exact", "[limiter]")
{
    const auto x = stressSignals()[0];
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        auto p = params();
        p.on = false;
        const auto r = runLimiter(x, q, p);
        const auto d = static_cast<size_t>(r.latency);
        bool exact = true;
        for (size_t i = 0; i < x.size(); ++i)
            exact = exact && r.out[i] == (i >= d ? x[i - d] : 0.0);
        CHECK(exact);
    }
}

TEST_CASE("the limiter does not allocate and does not depend on the block size", "[limiter]")
{
    const auto x = stressSignals()[1];
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        auto run = [&](size_t block) {
            Limiter lim;
            lim.prepare(kFs, static_cast<int>(block), q);
            lim.setParams(params(-1.0, q == Quality::Print));
            std::vector<double> y(x);
            size_t allocations = 0;
            for (size_t i = 0; i < y.size(); i += block)
            {
                test::AllocGuard guard;
                lim.process(std::span<double>(y.data() + i, std::min(block, y.size() - i)));
                allocations += guard.count();
            }
            CHECK(allocations == 0);
            return y;
        };
        const auto a = run(1);
        CHECK(run(64) == a);
        CHECK(run(997) == a);
    }
}

TEST_CASE("golden gain trajectory within 1e-9", "[limiter]")
{
    const auto golden = test::Golden::load("limiter.sgg");
    const auto x = concat({sig::sine(40.0, kFs, 0.2, 5.0), sig::sine(40.0, kFs, 0.2, -6.0)});
    auto compare = [](const std::vector<double>& got, std::span<const double> want) {
        REQUIRE((got.size() + 31) / 32 == want.size());
        double worst = 0.0;
        for (size_t i = 0; i < want.size(); ++i)
            worst = std::max(worst, std::abs(got[32 * i] - want[i]));
        return worst;
    };
    CHECK(compare(runLimiter(x, Quality::Live, params()).out, golden["y/live"]) <= 1e-9);
    for (const auto& [q, name] : {std::pair{Quality::Mix, "mix"}, std::pair{Quality::Print, "print"}})
    {
        INFO(name);
        const auto r = runLimiter(x, q, params());
        CHECK(compare(r.out, golden[std::string("y/") + name]) <= 1e-9);
        CHECK(compare(r.gain, golden[std::string("gain/") + name]) <= 1e-9);
    }
}
