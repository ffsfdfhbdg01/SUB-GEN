// SPDX-License-Identifier: AGPL-3.0-or-later
// HARMONIC (SPEC §6.5, T-HARM-01/02): the Chebyshev waveshaper on the max-hold envelope, against the prototype
// (tests/golden/harmonic.sgg, proto/subgen_proto/harmonic.py) and the TEST_PLAN criteria.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "core/Quality.h"
#include "engines/Harmonic.h"
#include "support/AllocGuard.h"
#include "support/Golden.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::engines;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

// The engine over x, aligned with the input (the lookahead removed).
std::vector<double> runHarmonic(const std::vector<double>& x, const EngineControl& control,
                                Quality quality = Quality::Live, double fs = kFs)
{
    Harmonic h;
    const int look = engineLookaheadSamples(quality, fs);
    h.prepare(fs, 1, look);
    h.setControl(control);
    std::vector<double> out(x.size() + static_cast<size_t>(look));
    const double key = 0.0;
    for (size_t n = 0; n < out.size(); ++n)
    {
        const double in = n < x.size() ? x[n] : 0.0;
        h.process(std::span<const double>(&in, 1), std::span<const double>(&key, 1), std::span<double>(&out[n], 1));
    }
    return {out.begin() + look, out.end()};
}

// A band wide enough to leave a 40-70 Hz source as it is (the prototype measured on the source itself).
EngineControl wideBand(double amount, double balance, int harmMax)
{
    EngineControl c;
    c.rangeLoHz = 15.0;
    c.rangeHiHz = 500.0;
    c.harmAmount = amount;
    c.harmBalance = balance;
    c.harmMax = harmMax;
    return c;
}

double dftMagnitude(std::span<const double> x, double f, double fs)
{
    std::complex<double> sum = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
        sum += x[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i) / fs);
    return 2.0 * std::abs(sum) / static_cast<double>(x.size());
}

double db(double v) { return 20.0 * std::log10(v); }
} // namespace

TEST_CASE("T-HARM-01 unit sine: amplitude of harmonic k equals w_k within 0.5 dB (K = 3/4/5, balance -100/0/100); DC "
          "<= -80 dBFS",
          "[harmonic]")
{
    // 40 Hz, -12 dBFS: the window 1-2 s holds whole cycles; w_k = amount c_k / k, c_k = 1 - b (even), 1 + b (odd)
    const double a = std::pow(10.0, -12.0 / 20.0);
    const auto x = sig::sine(40.0, kFs, 3.0, -12.0);
    for (const int harmMax : {3, 4, 5})
        for (const double balance : {-1.0, 0.0, 1.0})
        {
            INFO("K " << harmMax << ", balance " << balance);
            const auto y = runHarmonic(x, wideBand(1.0, balance, harmMax));
            const std::span<const double> seg(y.data() + 48000, 48000);
            const auto amps = test::harmonicAmps(seg, kFs, 40.0, 6);
            CHECK(amps[0] / a < std::pow(10.0, -50.0 / 20.0)); // the fundamental is not generated
            for (int k = 2; k <= 6; ++k)
            {
                const double c = std::max(k % 2 == 0 ? 1.0 - balance : 1.0 + balance, 0.0);
                const double w = k <= harmMax ? c / k : 0.0;
                const double rel = amps[static_cast<size_t>(k - 1)] / a;
                INFO("harmonic " << k << ": " << db(rel) << " dB, w_k " << w);
                if (w > 0.0)
                    CHECK(std::abs(db(rel) - db(w)) <= 0.5);
                else
                    CHECK(rel <= std::pow(10.0, -60.0 / 20.0));
            }
            double mean = 0.0;
            for (const double v : seg)
                mean += v / static_cast<double>(seg.size());
            CHECK(std::abs(mean) <= std::pow(10.0, -80.0 / 20.0));
        }
}

TEST_CASE("T-HARM-02 SIG-17 intermodulation share is reported (informational)", "[harmonic]")
{
    // 50 + 70 Hz, -12 dBFS peak, 100 %, K = 5: lines at multiples of 10 Hz over 1-2 s; P2-05: 72 % of the line power
    const auto y = runHarmonic(sig::twoTone(50.0, 70.0, kFs, 3.0, -12.0), wideBand(1.0, 0.0, 5));
    const std::span<const double> seg(y.data() + 48000, 48000);
    double all = 0.0;
    double harmonics = 0.0;
    for (int f = 10; f < 400; f += 10)
    {
        const double v = dftMagnitude(seg, f, kFs);
        if (v <= 1e-5)
            continue;
        all += v * v;
        bool isHarmonic = false;
        for (int m = 2; m <= 5; ++m)
            isHarmonic = isHarmonic || f == m * 50 || f == m * 70;
        harmonics += isHarmonic ? v * v : 0.0;
    }
    const double share = (all - harmonics) / all;
    INFO("intermodulation share " << 100.0 * share << " % (P2-05: 71.6 %)");
    CHECK(std::abs(share - 0.716) <= 0.03);
}

TEST_CASE("below the -80 dBFS floor the output falls 20 dB per 10 dB: -90 dBFS gives <= -100 dBFS", "[harmonic]")
{
    // under the envelope floor x_n is no longer a unit sine, and the lower-order terms of T_3 and T_4 leave a
    // fundamental and DC that scale with the square of the level (prototype: -105.7 / -125.4 dBFS)
    for (const auto& [level, limit] : {std::pair{-90.0, -100.0}, std::pair{-100.0, -120.0}})
    {
        const auto y = runHarmonic(sig::sine(55.0, kFs, 1.0, level), EngineControl{});
        double peak = 0.0;
        for (size_t i = 4800; i < y.size(); ++i)
            peak = std::max(peak, std::abs(y[i]));
        INFO(level << " dBFS in: output peak " << db(peak) << " dBFS");
        CHECK(db(peak) <= limit);
    }
}

TEST_CASE("HARMONIC is delayed by the lookahead, reports the source pitch, and Fixed bands around base_freq", "[harmonic]")
{
    const auto x = sig::sine(55.0, kFs, 1.0, -12.0);
    const auto live = runHarmonic(x, EngineControl{});
    const auto mix = runHarmonic(x, EngineControl{}, Quality::Mix); // aligned: the same samples
    CHECK(live == mix);
    Harmonic h;
    h.prepare(kFs, 512, 0);
    h.setControl(EngineControl{});
    std::vector<double> out(x.size());
    const std::vector<double> key(x.size(), 0.0);
    h.process(x, key, out);
    CHECK(std::abs(h.subFrequencyHz() - 55.0) <= 0.5); // the source f0 (the sub chain's windows use it)
    EngineControl fixed;
    fixed.baseFixed = true;
    fixed.baseFreqHz = 110.0; // the band is 110 Hz +- 1/2 octave: a 40 Hz source hardly passes
    const auto low = runHarmonic(sig::sine(40.0, kFs, 1.0, -12.0), fixed);
    const auto centre = runHarmonic(sig::sine(110.0, kFs, 1.0, -12.0), fixed);
    double pLow = 0.0, pCentre = 0.0;
    for (size_t i = 24000; i < low.size(); ++i)
    {
        pLow = std::max(pLow, std::abs(low[i]));
        pCentre = std::max(pCentre, std::abs(centre[i]));
    }
    CHECK(pCentre > 0.05);
    h.setControl(fixed);
    CHECK(h.subFrequencyHz() == 110.0);
    // the harmonic amplitudes are relative to the envelope, so a weak in-band source still gives full weights;
    // what Fixed changes is which source passes: 40 Hz is attenuated by the band before the envelope
    CHECK(pLow < pCentre);
}

TEST_CASE("HARMONIC clamps out-of-range controls and stays finite", "[harmonic]")
{
    EngineControl c;
    c.harmAmount = std::numeric_limits<double>::quiet_NaN();
    c.harmBalance = 7.0;
    c.harmMax = 99;
    c.rangeLoHz = 0.0;
    const auto y = runHarmonic(sig::square(55.0, kFs, 0.5, 6.0), c);
    bool finite = true;
    for (const double v : y)
        finite = finite && std::isfinite(v);
    CHECK(finite);
}

TEST_CASE("HARMONIC process does not allocate and does not depend on the block size", "[harmonic]")
{
    const auto x = sig::bass808(kFs);
    auto run = [&](size_t block) {
        Harmonic h;
        h.prepare(kFs, static_cast<int>(block), engineLookaheadSamples(Quality::Mix, kFs));
        h.setControl(EngineControl{});
        std::vector<double> out(x.size());
        const std::vector<double> key(block, 0.0);
        size_t allocations = 0;
        for (size_t i = 0; i < x.size(); i += block)
        {
            const size_t len = std::min(block, x.size() - i);
            test::AllocGuard guard;
            h.process(std::span<const double>(x.data() + i, len), std::span<const double>(key.data(), len),
                      std::span<double>(out.data() + i, len));
            allocations += guard.count();
        }
        CHECK(allocations == 0);
        return out;
    };
    const auto a = run(1);
    CHECK(run(64) == a);
    CHECK(run(997) == a);
}

TEST_CASE("golden within 1e-9", "[harmonic]")
{
    const auto golden = test::Golden::load("harmonic.sgg");
    EngineControl k5;
    k5.harmAmount = 0.8;
    k5.harmBalance = 0.5;
    k5.harmMax = 5;
    struct Case
    {
        const char* name;
        std::vector<double> input;
        EngineControl control;
        size_t first;
    };
    const std::vector<Case> cases{
        {"sine55", sig::sine(55.0, kFs, 0.3, -12.0), EngineControl{}, 0},
        {"two_tone", sig::twoTone(50.0, 70.0, kFs, 0.3, -12.0), EngineControl{}, 8192},
        {"bass808_k5", sig::bass808(kFs), k5, 0},
    };
    for (const auto& c : cases)
    {
        INFO(c.name);
        const auto want = golden[std::string("out/") + c.name];
        const auto y = runHarmonic(c.input, c.control);
        double worst = 0.0;
        for (size_t i = 0; i < want.size(); ++i)
            worst = std::max(worst, std::abs(y[c.first + i] - want[i]));
        CHECK(worst <= 1e-9);
    }
}
