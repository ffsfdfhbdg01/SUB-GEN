// SPDX-License-Identifier: AGPL-3.0-or-later
// Envelope followers, sliding maximum and mean square, smoothers and delay lines.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "dsp/DelayLine.h"
#include "dsp/Envelope.h"
#include "dsp/Smoother.h"
#include "support/AllocGuard.h"
#include "support/Golden.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::dsp;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

double maxAbsDiff(const std::vector<double>& a, std::span<const double> b)
{
    REQUIRE(a.size() == b.size());
    double m = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}

template <typename F>
std::vector<double> run(const std::vector<double>& x, F&& f)
{
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = f(x[i]);
    return y;
}
} // namespace

TEST_CASE("golden followers and sliding max on a kick and xorshift noise within 1e-12", "[envelope]")
{
    const auto golden = test::Golden::load("envelope.sgg");
    auto kick = sig::kick(kFs);
    kick.resize(4096);
    const auto noise = sig::xorshiftNoise(4096, 13);
    for (const auto& [name, x] : {std::pair{"kick", kick}, std::pair{"noise", noise}})
    {
        INFO(name);
        PeakFollower slow, fast;
        slow.prepare(kFs, 1.0, 50.0);
        fast.prepare(kFs, 0.2, 15.0);
        RmsFollower rms;
        rms.prepare(kFs, 20.0);
        SlidingMax hold;
        hold.prepare(480);
        hold.setWindow(480);
        CHECK(maxAbsDiff(run(x, [&](double v) { return slow.process(v); }), golden[std::string("peak_1_50/") + name])
              <= 1e-12);
        CHECK(maxAbsDiff(run(x, [&](double v) { return fast.process(v); }), golden[std::string("peak_0.2_15/") + name])
              <= 1e-12);
        CHECK(maxAbsDiff(run(x, [&](double v) { return rms.process(v); }), golden[std::string("rms_20/") + name])
              <= 1e-12);
        // the maximum itself is exact: equal to the bit for the noise, whose input is bit-identical to Python's
        const double holdError = maxAbsDiff(run(x, [&](double v) { return hold.process(std::abs(v)); }),
                                            golden[std::string("sliding_peak_480/") + name]);
        CHECK(holdError <= 1e-12);
        if (std::string(name) == "noise")
            CHECK(holdError == 0.0);
    }
}

TEST_CASE("SlidingMax equals brute force on 1e5 random samples with window changes", "[envelope]")
{
    constexpr int maxWindow = 700;
    const auto x = sig::xorshiftNoise(100000, 17);
    const auto windows = sig::xorshiftNoise(200, 19);
    SlidingMax hold;
    hold.prepare(maxWindow);
    hold.reset(-0.5); // samples before the start count as -0.5
    int mismatches = 0;
    for (size_t n = 0; n < x.size(); ++n)
    {
        const int w = 1 + static_cast<int>((windows[n / 500] * 0.5 + 0.5) * (maxWindow - 1)); // new window every 500
        hold.setWindow(w);
        const double y = hold.process(x[n]);
        double ref = n + 1 < static_cast<size_t>(w) ? -0.5 : -1e300;
        for (size_t j = n + 1 >= static_cast<size_t>(w) ? n + 1 - static_cast<size_t>(w) : 0; j <= n; ++j)
            ref = std::max(ref, x[j]);
        mismatches += y != ref;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("SlidingMax after reset(1.0) on zeros gives 1.0 for the first w - 1 samples, then 0.0", "[envelope]")
{
    SlidingMax hold;
    hold.prepare(16);
    hold.setWindow(4);
    hold.reset(1.0);
    std::vector<double> y;
    for (int i = 0; i < 8; ++i)
        y.push_back(hold.process(0.0));
    CHECK(y == std::vector<double>{1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0});
}

TEST_CASE("SlidingMeanSquare drifts less than 1e-12 after 1e7 samples", "[envelope]")
{
    constexpr int w = 1000;
    SlidingMeanSquare ms;
    ms.prepare(w);
    ms.setWindow(w);
    std::vector<double> last(w);
    double y = 0.0;
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (int n = 0; n < 10'000'000; ++n)
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        const double v = static_cast<double>((state * 2685821657736338717ULL) >> 11) / 9007199254740992.0 * 2.0 - 1.0;
        last[static_cast<size_t>(n % w)] = v;
        y = ms.process(v);
    }
    double exact = 0.0;
    for (const double v : last)
        exact += v * v;
    exact /= w;
    CHECK(std::abs(y - exact) <= 1e-12);
}

TEST_CASE("SlidingMeanSquare follows window changes", "[envelope]")
{
    SlidingMeanSquare ms;
    ms.prepare(100);
    ms.setWindow(100);
    for (int i = 0; i < 100; ++i)
        ms.process(i < 50 ? 1.0 : 2.0); // last 50 samples are 2.0
    ms.setWindow(50);
    CHECK(std::abs(ms.process(2.0) - 4.0) <= 1e-15); // window of the last 50 samples, all 2.0
}

TEST_CASE("GainSmoother reaches its target within 20 ms (1e-3) and exactly 0.0 after Off", "[envelope]")
{
    const int steps = static_cast<int>(std::lround(0.020 * kFs));
    GainSmoother g;
    g.reset(kFs, 20.0, 1.0);
    g.setTarget(0.25);
    double v = 0.0;
    for (int i = 0; i < steps; ++i)
        v = g.next();
    CHECK(std::abs(v / 0.25 - 1.0) <= 1e-3);
    CHECK(!g.isSmoothing());
    g.setTarget(0.0);
    for (int i = 0; i < steps; ++i)
        v = g.next();
    CHECK(v == 0.0);
    CHECK(g.current() == 0.0);
    g.setTarget(0.5); // up from exactly zero
    for (int i = 0; i < steps; ++i)
        v = g.next();
    CHECK(std::abs(v / 0.5 - 1.0) <= 1e-3);

    LinearSmoother l;
    l.reset(kFs, 20.0, 0.0);
    l.setTarget(1.0);
    for (int i = 0; i < steps; ++i)
        v = l.next();
    CHECK(v == 1.0);

    LogSmoother f;
    f.reset(kFs, 20.0, 40.0);
    f.setTarget(80.0);
    double half = 0.0;
    for (int i = 0; i < steps; ++i)
        if (f.next(); i == steps / 2 - 1)
            half = f.current();
    CHECK(f.current() == 80.0);
    CHECK(std::abs(half / std::sqrt(40.0 * 80.0) - 1.0) <= 1e-3); // halfway in log frequency
}

TEST_CASE("DelayLine delays exactly", "[envelope]")
{
    const auto x = sig::xorshiftNoise(2000, 23);
    for (const int d : {0, 1, 480, 1000})
    {
        DelayLine line;
        line.prepare(1000);
        line.setDelay(d);
        bool exact = true;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double y = line.process(x[i]);
            exact = exact && y == (i >= static_cast<size_t>(d) ? x[i - static_cast<size_t>(d)] : 0.0);
        }
        INFO("delay " << d);
        CHECK(exact);
    }
}

TEST_CASE("envelope process paths do not allocate", "[envelope]")
{
    PeakFollower peak;
    peak.prepare(kFs, 1.0, 50.0);
    RmsFollower rms;
    rms.prepare(kFs, 20.0);
    SlidingMax hold;
    hold.prepare(4800);
    SlidingMeanSquare ms;
    ms.prepare(4800);
    DelayLine line;
    line.prepare(4800);
    GainSmoother g;
    g.reset(kFs, 20.0, 1.0);
    LinearSmoother l;
    l.reset(kFs, 20.0, 0.0);
    LogSmoother f;
    f.reset(kFs, 20.0, 50.0);
    double sink = 0.0;
    size_t allocations = 0;
    {
        test::AllocGuard guard;
        for (int i = 0; i < 20000; ++i)
        {
            const double v = (i % 97) * 0.01 - 0.5;
            if (i % 1000 == 0)
            {
                hold.setWindow(1 + i % 4800);
                ms.setWindow(1 + i % 4800);
                line.setDelay(i % 4800);
                g.setTarget(i % 2000 == 0 ? 0.0 : 0.7);
                l.setTarget(v);
                f.setTarget(30.0 + i % 300);
            }
            sink += peak.process(v) + rms.process(v) + hold.process(v) + ms.process(v) + line.process(v) + g.next()
                  + l.next() + f.next();
        }
        hold.reset(-1.0);
        allocations = guard.count();
    }
    CHECK(allocations == 0);
    CHECK(std::isfinite(sink));
}
