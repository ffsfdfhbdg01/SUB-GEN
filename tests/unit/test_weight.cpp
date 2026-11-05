// SPDX-License-Identifier: AGPL-3.0-or-later
// Weight: the low bump with harmonics on the dry (docs/superpowers/specs/2026-11-05-subgen-resynth-weight-design.md):
// 0 dB is the exact identity, the bump sits at weight_freq with weight_amount, the highs stay, harmonics come with it.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include "chain/Weight.h"
#include "support/AllocGuard.h"
#include "support/Measure.h"

using namespace subgen;

namespace
{
constexpr double kFs = 48000.0;

std::vector<double> sine(double hz, double amplitude, double seconds)
{
    std::vector<double> x(static_cast<size_t>(seconds * kFs));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kFs);
    return x;
}

std::vector<double> through(chain::Weight& w, std::vector<double> x)
{
    w.process(x);
    return x;
}

// the gain in dB of the last half second at hz
double gainDb(const std::vector<double>& in, const std::vector<double>& out, double hz)
{
    const auto tail = [](const std::vector<double>& v) { return std::span<const double>(v).last(static_cast<size_t>(0.5 * kFs)); };
    return 20.0 * std::log10(test::harmonicAmps(tail(out), kFs, hz, 1)[0] / test::harmonicAmps(tail(in), kFs, hz, 1)[0]);
}

chain::Weight made(double amountDb, double freqHz)
{
    chain::Weight w;
    w.prepare(kFs);
    w.setParams(amountDb, freqHz);
    return w;
}
} // namespace

TEST_CASE("Weight at 0 dB is the exact identity", "[weight]")
{
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0.0, 0.3);
    std::vector<double> x(48000);
    for (auto& v : x)
        v = noise(rng);
    auto w = made(0.0, 80.0);
    CHECK(through(w, x) == x);
}

TEST_CASE("Weight lifts its frequency by its amount and leaves the highs", "[weight]")
{
    for (const auto& [amount, freq] : {std::pair{6.0, 80.0}, std::pair{10.0, 150.0}, std::pair{3.0, 60.0}})
    {
        INFO(amount << " dB at " << freq << " Hz");
        const auto low = sine(freq, 0.05, 1.5); // quiet: the harmonics stay small
        auto w1 = made(amount, freq);
        CHECK(gainDb(low, through(w1, low), freq) == Catch::Approx(amount).margin(0.5));
        const auto high = sine(1000.0, 0.05, 1.5);
        auto w2 = made(amount, freq);
        CHECK(std::abs(gainDb(high, through(w2, high), 1000.0)) < 0.5);
    }
}

TEST_CASE("Weight adds harmonics of the low end, more with more weight", "[weight]")
{
    const auto x = sine(80.0, 0.5, 1.5);
    auto level = [&](double amount) {
        auto w = made(amount, 80.0);
        const auto y = through(w, x);
        const auto h = test::harmonicAmps(std::span<const double>(y).last(static_cast<size_t>(0.5 * kFs)), kFs, 80.0, 3);
        return 20.0 * std::log10((h[1] + h[2]) / h[0]);
    };
    const double light = level(3.0), heavy = level(10.0);
    CHECK(heavy > -40.0); // audible on small speakers
    CHECK(heavy < -12.0); // not a distortion effect
    CHECK(heavy > light + 3.0);
}

TEST_CASE("Weight changes smoothly and does not allocate", "[weight]")
{
    auto w = made(0.0, 80.0);
    auto x = sine(80.0, 0.5, 0.2);
    std::size_t allocations = 0;
    {
        test::AllocGuard guard;
        w.setParams(10.0, 120.0);
        w.process(x);
        allocations = guard.count();
    }
    CHECK(allocations == 0);
    double jump = 0.0;
    for (size_t i = 1; i < x.size(); ++i)
        jump = std::max(jump, std::abs(x[i] - x[i - 1]));
    CHECK(jump < 0.05); // an 80 Hz sine at 0.5 moves at most 0.0052 a sample; a step in gain would show
}
