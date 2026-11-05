// SPDX-License-Identifier: AGPL-3.0-or-later
// The phase tools (SPEC §7.7; T-FILT-05, T-PHS-01, T-PHS-02): the all-pass Hilbert pair, rotation y = cos(theta) A +
// sin(theta) B with theta = 0 being chain A, the polarity switch, chain A's group delay.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "chain/PhaseTool.h"
#include "support/AllocGuard.h"
#include "support/Golden.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::chain;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

std::vector<double> impulse(size_t n)
{
    std::vector<double> x(n, 0.0);
    x[0] = 1.0;
    return x;
}

std::complex<double> dft(const std::vector<double>& h, double f, double fs)
{
    std::complex<double> sum = 0.0;
    for (size_t n = 0; n < h.size(); ++n)
        sum += h[n] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / fs);
    return sum;
}

double wrapDeg(double a) { return std::remainder(a, 360.0); }

double degOf(std::complex<double> z) { return std::arg(z) * 180.0 / std::numbers::pi; }

std::vector<double> rotated(double thetaDeg, bool invert, const std::vector<double>& x, size_t block = 64)
{
    PhaseTool tool;
    tool.prepare(kFs);
    tool.setParams({invert, thetaDeg});
    std::vector<double> y(x);
    for (size_t i = 0; i < y.size(); i += block)
        tool.process(std::span<double>(y.data() + i, std::min(block, y.size() - i)));
    return y;
}

double maxFirstDifference(const std::vector<double>& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t>(from, 1); i < to; ++i)
        worst = std::max(worst, std::abs(y[i] - y[i - 1]));
    return worst;
}
} // namespace

TEST_CASE("T-FILT-05 B leads A by 90 +- 0.2 deg over 12-600 Hz at 44.1/48/96 kHz; branch magnitudes 0 +- 0.001 dB",
          "[phase]")
{
    for (const double fs : {44100.0, 48000.0, 96000.0})
    {
        HilbertPair pair;
        pair.prepare(fs);
        const auto x = impulse(static_cast<size_t>(2.0 * fs)); // both chains have rung out within 2 s
        std::vector<double> a(x.size()), b(x.size());
        for (size_t n = 0; n < x.size(); ++n)
            pair.process(x[n], a[n], b[n]);
        double worstPhase = 0.0, worstMag = 0.0;
        for (int i = 0; i <= 60; ++i)
        {
            const double f = 12.0 * std::pow(600.0 / 12.0, i / 60.0);
            const auto ha = dft(a, f, fs), hb = dft(b, f, fs);
            worstPhase = std::max(worstPhase, std::abs(wrapDeg(degOf(hb) - degOf(ha) - 90.0)));
            worstMag = std::max({worstMag, std::abs(20.0 * std::log10(std::abs(ha))), std::abs(20.0 * std::log10(std::abs(hb)))});
        }
        INFO("fs " << fs << ": phase error " << worstPhase << " deg, magnitude " << worstMag << " dB");
        CHECK(worstPhase <= 0.2);
        CHECK(worstMag <= 0.001);
    }
}

TEST_CASE("T-PHS-01 the sum with an inverted copy is exactly 0", "[phase]")
{
    const auto x = sig::bass808(kFs);
    for (const double theta : {0.0, 37.0, -120.0})
    {
        const auto y = rotated(theta, false, x);
        const auto z = rotated(theta, true, x);
        bool zero = true;
        for (size_t i = 0; i < y.size(); ++i)
            zero = zero && y[i] + z[i] == 0.0;
        CHECK(zero);
    }
}

TEST_CASE("T-PHS-02 rotation in 30-300 Hz: phase +- 0.2 deg, amplitude +- 0.02 dB", "[phase]")
{
    const auto x = impulse(96000);
    const auto chain = rotated(0.0, false, x);
    double worstPhase = 0.0, worstAmp = 0.0;
    for (const double theta : {-180.0, -135.0, -90.0, -45.0, 30.0, 90.0, 135.0, 180.0})
    {
        const auto y = rotated(theta, false, x);
        for (const double f : {30.0, 45.0, 60.0, 100.0, 150.0, 200.0, 300.0})
        {
            const auto ratio = dft(y, f, kFs) / dft(chain, f, kFs); // against theta = 0, chain A
            worstPhase = std::max(worstPhase, std::abs(wrapDeg(degOf(ratio) - theta)));
            worstAmp = std::max(worstAmp, std::abs(20.0 * std::log10(std::abs(ratio))));
        }
    }
    INFO("phase error " << worstPhase << " deg, amplitude " << worstAmp << " dB");
    CHECK(worstPhase <= 0.2);
    CHECK(worstAmp <= 0.02);
}

TEST_CASE("theta 0 equals chain A (golden, 1e-12)", "[phase]")
{
    const auto golden = test::Golden::load("phase.sgg");
    const auto goldenA = golden["a"];
    const auto goldenB = golden["b"];
    const auto x = impulse(4096);
    auto worst = [](std::span<const double> ref, const std::vector<double>& got) {
        double w = 0.0;
        for (size_t i = 0; i < ref.size(); ++i)
            w = std::max(w, std::abs(ref[i] - got[i]));
        return w;
    };
    CHECK(worst(goldenA, rotated(0.0, false, x)) <= 1e-12);
    PhaseTool tool;
    tool.prepare(kFs);
    std::vector<double> viaChainA(x.size());
    tool.chainA(x, viaChainA);
    CHECK(worst(goldenA, viaChainA) <= 1e-12);
    HilbertPair pair;
    pair.prepare(kFs);
    std::vector<double> a(x.size()), b(x.size());
    for (size_t n = 0; n < x.size(); ++n)
        pair.process(x[n], a[n], b[n]);
    CHECK(worst(goldenA, a) <= 1e-12);
    CHECK(worst(goldenB, b) <= 1e-12);
}

TEST_CASE("chain A group delay 9.8/6.2/3.2 ms at 30/50/100 Hz +- 0.1 ms", "[phase]")
{
    PhaseTool tool;
    tool.prepare(kFs);
    const auto h = rotated(0.0, false, impulse(96000));
    for (const auto& [f, ms] : {std::pair{30.0, 9.8}, std::pair{50.0, 6.2}, std::pair{100.0, 3.2}})
    {
        std::complex<double> num = 0.0, den = 0.0;
        for (size_t n = 0; n < h.size(); ++n)
        {
            const auto e = std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / kFs);
            num += static_cast<double>(n) * h[n] * e;
            den += h[n] * e;
        }
        const double measured = 1000.0 * (num / den).real() / kFs;
        INFO(f << " Hz: displayed " << tool.groupDelayMs(f) << " ms, measured " << measured << " ms");
        CHECK(std::abs(tool.groupDelayMs(f) - ms) <= 0.1);
        CHECK(std::abs(tool.groupDelayMs(f) - measured) <= 1e-3);
    }
}

TEST_CASE("rotation and polarity move without clicks", "[phase]")
{
    // theta moves linearly over 20 ms and the polarity crossfades over 5 ms: no step in the waveform (a hard switch
    // jumps by up to twice the amplitude)
    const auto x = sig::sine(55.0, kFs, 0.75, -6.0);
    PhaseTool tool;
    tool.prepare(kFs);
    PhaseParams p;
    tool.setParams(p);
    std::vector<double> y(x);
    for (size_t i = 0; i < y.size(); i += 64)
    {
        if (i == 12032)
            p.rotationDeg = 120.0;
        if (i == 18048)
            p.invert = true;
        if (i == 24064)
            p.rotationDeg = -60.0;
        if (i == 30080)
            p.invert = false;
        if (i == 12032 || i == 18048 || i == 24064 || i == 30080)
            tool.setParams(p);
        tool.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
    }
    const double steady = maxFirstDifference(y, 6000, 12000);
    for (const size_t from : {12000, 18000, 24000, 30000})
    {
        INFO("steady " << steady << ", from " << from << ": " << maxFirstDifference(y, from, from + 6000));
        CHECK(maxFirstDifference(y, from, from + 6000) <= 3.0 * steady);
    }
    // the end state: theta -60, not inverted
    const auto end = rotated(-60.0, false, x);
    CHECK(std::abs(y.back() - end.back()) <= 1e-9);
}

TEST_CASE("the phase tool does not allocate and does not depend on the block size", "[phase]")
{
    const auto x = sig::bass808(kFs);
    auto go = [&](size_t block) {
        PhaseTool tool;
        tool.prepare(kFs);
        PhaseParams p{false, 45.0};
        tool.setParams(p);
        std::vector<double> y(x);
        size_t allocations = 0;
        for (size_t i = 0; i < y.size(); i += block)
        {
            if (i >= 9600 && i < 9600 + block)
            {
                p = {true, -90.0};
                tool.setParams(p);
            }
            const auto s = std::span<double>(y.data() + i, std::min(block, y.size() - i));
            test::AllocGuard guard;
            tool.process(s);
            allocations += guard.count();
        }
        CHECK(allocations == 0);
        return y;
    };
    const auto a = go(64);
    CHECK(go(32) == a); // both divide 9600
    CHECK(go(320) == a);
}
