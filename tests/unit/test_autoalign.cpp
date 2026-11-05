// SPDX-License-Identifier: AGPL-3.0-or-later
// Auto-Align (SPEC §8.B, T-PHS-03): the polarity and rotation that make the sub after chain A add best to the dry
// band; the capture buffer feeding it.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include "analysis/AutoAlign.h"
#include "analysis/CaptureBuffer.h"
#include "support/Golden.h"

using namespace subgen;
using namespace subgen::analysis;

namespace
{
constexpr double kFs = 48000.0;

double wrapDeg(double a)
{
    const double m = std::fmod(a + 180.0, 360.0);
    return (m < 0.0 ? m + 360.0 : m) - 180.0;
}

// the effective rotation of a result: theta, or theta + 180 with the polarity flipped
double psiOf(const AlignResult& r) { return wrapDeg(r.rotationDeg + (r.polarity < 0 ? 180.0 : 0.0)); }

// The prototype's scenario: the dry's 40 Hz component leads the sub by phiDeg, so the best rotation is phiDeg.
void scenario(double phiDeg, size_t n, std::vector<double>& dry, std::vector<double>& sub)
{
    dry.resize(n);
    sub.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kFs;
        dry[i] = std::sin(2.0 * std::numbers::pi * 80.0 * t) + 0.5 * std::sin(2.0 * std::numbers::pi * 40.0 * t + phiDeg * std::numbers::pi / 180.0);
        sub[i] = std::sin(2.0 * std::numbers::pi * 40.0 * t);
    }
}

std::vector<double> toVector(std::span<const double> s) { return {s.begin(), s.end()}; }
} // namespace

TEST_CASE("T-PHS-03 48 synthetic optima found within 15 deg", "[autoalign]")
{
    std::vector<double> dry, sub;
    double worst = 0.0, lowest = 1e9;
    for (int k = 0; k < 48; ++k)
    {
        const double phi = -180.0 + 7.5 * k; // on the grid and half way between
        scenario(phi, 96000, dry, sub);
        const auto r = autoAlign(dry, sub, kFs, 30.0, 120.0, AlignObjective::Energy);
        worst = std::max(worst, std::abs(wrapDeg(psiOf(r) - phi)));
        lowest = std::min(lowest, r.confidenceDb);
        CHECK(std::abs(r.rotationDeg) <= 90.0);
        CHECK(r.clearOptimum);
    }
    INFO("worst " << worst << " deg, lowest confidence " << lowest << " dB");
    CHECK(worst <= 15.0);
}

TEST_CASE("no overlap gives confidence < 0.5 dB (no clear optimum)", "[autoalign]")
{
    std::vector<double> dry(96000), sub(96000);
    for (size_t i = 0; i < dry.size(); ++i)
    {
        const double t = static_cast<double>(i) / kFs;
        dry[i] = std::sin(2.0 * std::numbers::pi * 80.0 * t); // nothing at the sub's frequency
        sub[i] = std::sin(2.0 * std::numbers::pi * 40.0 * t);
    }
    const auto r = autoAlign(dry, sub, kFs, 30.0, 120.0, AlignObjective::Energy);
    CHECK(r.confidenceDb < 0.5);
    CHECK_FALSE(r.clearOptimum);
    CHECK(r.polarity == 1);
    CHECK(r.rotationDeg == 0.0);
}

TEST_CASE("silent capture: polarity +1, rotation 0, confidence 0 dB, no NaN", "[autoalign]")
{
    const std::vector<double> silent(48000, 0.0);
    for (const auto objective : {AlignObjective::Energy, AlignObjective::Headroom})
    {
        const auto r = autoAlign(silent, silent, kFs, 30.0, 120.0, objective);
        CHECK(r.polarity == 1);
        CHECK(r.rotationDeg == 0.0);
        CHECK(r.confidenceDb == 0.0);
        CHECK(r.gainDb == 0.0);
        CHECK_FALSE(r.clearOptimum);
    }
    const std::vector<double> nothing;
    CHECK(autoAlign(nothing, nothing, kFs, 30.0, 120.0, AlignObjective::Energy).polarity == 1);
}

TEST_CASE("canonical form and the tie rule", "[autoalign]")
{
    // effective rotations off the +-90 interval come back as a polarity flip and |theta| <= 90; -90 is reported
    // as +90 with the flip, +90 as +90
    std::vector<double> dry, sub;
    for (const auto& [phi, pol, theta] : {std::tuple{0.0, 1, 0.0}, std::tuple{105.0, -1, -75.0}, std::tuple{180.0, -1, 0.0},
                                          std::tuple{-90.0, -1, 90.0}, std::tuple{90.0, 1, 90.0}, std::tuple{-75.0, 1, -75.0}})
    {
        INFO("phi " << phi);
        scenario(phi, size_t{1} << 17, dry, sub);
        const auto r = autoAlign(dry, sub, kFs, 30.0, 120.0, AlignObjective::Energy);
        CHECK(r.polarity == pol);
        CHECK(r.rotationDeg == theta);
    }
    // no dry: every rotation gives the sub's own energy, a tie: the identity wins
    std::vector<double> none(size_t{1} << 17, 0.0);
    scenario(0.0, size_t{1} << 17, dry, sub);
    const auto tie = autoAlign(none, sub, kFs, 30.0, 120.0, AlignObjective::Energy);
    CHECK(tie.polarity == 1);
    CHECK(tie.rotationDeg == 0.0);
    CHECK(tie.confidenceDb < 1e-3);
}

TEST_CASE("golden SIG-18 results (polarity, rotation)", "[autoalign]")
{
    const auto golden = test::Golden::load("autoalign.sgg");
    std::vector<double> dry, sub;
    for (const double phi : {0.0, 60.0, 180.0, -97.5})
        for (const auto& [name, objective] : {std::pair{"energy", AlignObjective::Energy}, std::pair{"headroom", AlignObjective::Headroom}})
        {
            std::string key = "synthetic/";
            key += phi == -97.5 ? "-97.5" : std::to_string(static_cast<int>(phi));
            key += std::string("/") + name;
            INFO(key);
            const auto want = golden[key];
            scenario(phi, size_t{1} << 17, dry, sub);
            const auto r = autoAlign(dry, sub, kFs, 30.0, 120.0, objective);
            CHECK(r.polarity == static_cast<int>(want[0]));
            CHECK(r.rotationDeg == want[1]);
            CHECK(std::abs(r.gainDb - want[2]) <= 1e-6);
            CHECK(std::abs(r.confidenceDb - want[3]) <= 1e-6);
        }
    // SIG-18 decimated to 1500 Hz: the dry band and the subs after chain A, as the plug-in captures them
    const auto sigDry = toVector(golden["sig18/dry"]);
    for (const std::string source : {"divide", "trigger"})
        for (const auto& [name, objective] : {std::pair{"energy", AlignObjective::Energy}, std::pair{"headroom", AlignObjective::Headroom}})
        {
            const std::string key = "sig18/" + source + "/" + name;
            INFO(key);
            const auto want = golden[key];
            const auto r = autoAlign(sigDry, toVector(golden["sig18/" + source]), 1500.0, 30.0, 120.0, objective);
            CHECK(r.polarity == static_cast<int>(want[0]));
            CHECK(r.rotationDeg == want[1]);
            CHECK(std::abs(r.gainDb - want[2]) <= 1e-6);
            CHECK(std::abs(r.confidenceDb - want[3]) <= 1e-6);
        }
}

TEST_CASE("SIG-18: the 15 deg grid is within 0.5 dB of a 1 deg search", "[autoalign]")
{
    const auto golden = test::Golden::load("autoalign.sgg");
    const auto sigDry = toVector(golden["sig18/dry"]);
    for (const std::string source : {"divide", "trigger"})
        for (const auto objective : {AlignObjective::Energy, AlignObjective::Headroom})
        {
            const auto sub = toVector(golden["sig18/" + source]);
            const auto coarse = autoAlign(sigDry, sub, 1500.0, 30.0, 120.0, objective);
            const auto fine = autoAlign(sigDry, sub, 1500.0, 30.0, 120.0, objective, 1.0);
            INFO(source << ": 15 deg " << coarse.gainDb << " dB, 1 deg " << fine.gainDb << " dB");
            CHECK(fine.gainDb - coarse.gainDb <= 0.5);
            CHECK(fine.gainDb >= coarse.gainDb - 1e-3); // a tie within 1e-3 dB may go to a rotation nearer the identity
        }
}

TEST_CASE("the capture buffer keeps the last seconds, lock-free for the audio thread", "[autoalign]")
{
    CaptureBuffer capture;
    capture.prepare(1000.0, 2.0); // 2000 samples
    std::vector<double> dry, sub;
    CHECK_FALSE(capture.snapshot(dry, sub));
    std::vector<double> a(300), b(300);
    double next = 0.0;
    for (int block = 0; block < 10; ++block) // 3000 samples: the oldest 1000 are gone
    {
        for (size_t i = 0; i < a.size(); ++i)
        {
            a[i] = next;
            b[i] = -next;
            next += 1.0;
        }
        capture.push(a, b);
    }
    REQUIRE(capture.snapshot(dry, sub));
    REQUIRE(dry.size() == 2000);
    CHECK(dry.front() == 1000.0);
    CHECK(dry.back() == 2999.0);
    CHECK(sub.back() == -2999.0);
    // a writer racing the reader: every snapshot is one contiguous run of what was pushed
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        std::vector<double> x(64), y(64);
        double v = 3000.0;
        while (!stop.load())
        {
            for (size_t i = 0; i < x.size(); ++i)
            {
                x[i] = v;
                y[i] = -v;
                v += 1.0;
            }
            capture.push(x, y);
        }
    });
    bool contiguous = true;
    for (int k = 0; k < 200; ++k)
    {
        if (!capture.snapshot(dry, sub))
            continue;
        for (size_t i = 1; i < dry.size(); ++i)
            contiguous = contiguous && dry[i] == dry[i - 1] + 1.0 && sub[i] == -dry[i];
    }
    stop.store(true);
    writer.join();
    CHECK(contiguous);
}

TEST_CASE("zeroPhaseBand keeps an in-band tone's phase and removes out-of-band tones", "[autoalign]")
{
    constexpr double fs = 48000.0;
    auto tone = [&](double f) {
        std::vector<double> x(96000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.5 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / fs);
        return x;
    };
    for (const double f : {30.0, 40.0, 60.0, 100.0}) // in [15, 120] Hz
    {
        INFO(f << " Hz");
        const auto x = tone(f);
        const auto y = zeroPhaseBand(x, fs, 15.0, 120.0);
        REQUIRE(y.size() == x.size());
        // y = g x away from the ends, g = |H(f)|^2 real: no phase shift (a minimum-phase band turns 100 Hz by -121 deg)
        double xy = 0.0, xx = 0.0;
        for (size_t i = 24000; i < 72000; ++i)
        {
            xy += x[i] * y[i];
            xx += x[i] * x[i];
        }
        const double g = xy / xx;
        double residual = 0.0;
        for (size_t i = 24000; i < 72000; ++i)
            residual += (y[i] - g * x[i]) * (y[i] - g * x[i]);
        CHECK(10.0 * std::log10(residual / (g * g * xx)) < -60.0);
        CHECK(g > 0.75);
        CHECK(g < 1.0 + 1e-9);
    }
    for (const double f : {3.0, 600.0})
    {
        INFO(f << " Hz");
        const auto x = tone(f);
        const auto y = zeroPhaseBand(x, fs, 15.0, 120.0);
        double yy = 0.0, xx = 0.0;
        for (size_t i = 24000; i < 72000; ++i)
        {
            yy += y[i] * y[i];
            xx += x[i] * x[i];
        }
        CHECK(10.0 * std::log10(yy / xx) < -60.0);
    }
    // a NaN in the capture counts as 0, as in autoAlign
    auto x = tone(40.0);
    x[100] = std::numeric_limits<double>::quiet_NaN();
    for (const double v : zeroPhaseBand(x, fs, 15.0, 120.0))
        REQUIRE(std::isfinite(v));
}
