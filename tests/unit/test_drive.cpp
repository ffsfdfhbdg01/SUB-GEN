// SPDX-License-Identifier: AGPL-3.0-or-later
// Drive/Saturate (SPEC §7.3, T-DRV-01..05, T-ALIAS-01): five characters with ADAA, JUCE oversampling in Mix and
// Print, the Fold depth cap, Tone, the 5 Hz high-pass and Auto Gain, against the prototype's 16x reference
// (tests/golden/drive.sgg, proto/subgen_proto/drive.py) and the TEST_PLAN criteria.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

#include "chain/AutoGainTable.h"
#include "chain/Drive.h"
#include "chain/DriveShapes.h"
#include "core/Quality.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "support/AllocGuard.h"
#include "support/Dft.h"
#include "support/Golden.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::chain;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;
constexpr DriveMode kModes[] = {DriveMode::Soft, DriveMode::Tube, DriveMode::Tape, DriveMode::Fold, DriveMode::Hard};

const char* modeName(DriveMode m)
{
    switch (m)
    {
    case DriveMode::Soft:
        return "soft";
    case DriveMode::Tube:
        return "tube";
    case DriveMode::Tape:
        return "tape";
    case DriveMode::Fold:
        return "fold";
    case DriveMode::Hard:
        return "hard";
    }
    return "";
}

const char* qualityName(Quality q) { return q == Quality::Live ? "Live" : (q == Quality::Mix ? "Mix" : "Print"); }

// The stage over x in blocks, aligned with the input (its latency removed).
std::vector<double> runDrive(const std::vector<double>& x, Quality q, const DriveParams& p, double fs = kFs,
                             size_t block = 512)
{
    Drive drive;
    drive.prepare(fs, static_cast<int>(block), q);
    drive.setParams(p);
    const auto latency = static_cast<size_t>(drive.latencySamples());
    std::vector<double> buf(x);
    buf.resize(x.size() + latency, 0.0);
    for (size_t i = 0; i < buf.size(); i += block)
        drive.process(std::span<double>(buf.data() + i, std::min(block, buf.size() - i)));
    return {buf.begin() + static_cast<std::ptrdiff_t>(latency), buf.end()};
}

// The 16x reference's settings: no Tone, no Auto Gain, f_sub1 40 Hz.
DriveParams bare(DriveMode mode, double drivePct)
{
    DriveParams p;
    p.drivePct = drivePct;
    p.mode = mode;
    p.tone = false;
    p.autoGain = false;
    p.fSub1Hz = 40.0;
    return p;
}

// Amplitudes of harmonics 1..n of f0 over the second [fs, 2 fs) (bins = Hz).
std::vector<double> lines(const std::vector<double>& y, double fs, double f0, int n)
{
    const auto len = static_cast<size_t>(fs);
    std::vector<double> amps;
    for (int k = 1; k <= n; ++k)
    {
        std::complex<double> sum = 0.0;
        const double w = -2.0 * std::numbers::pi * k * f0 / fs;
        for (size_t i = 0; i < len; ++i)
            sum += y[len + i] * std::polar(1.0, w * static_cast<double>(i));
        amps.push_back(2.0 * std::abs(sum) / static_cast<double>(len));
    }
    return amps;
}

// T-ALIAS-01: power of the components that are not harmonics of f0 over all, in [20 Hz, fHi], on the central part
// of y (0.25 s cut off both ends; it must hold whole cycles of f0).
double nonharmonicDb(const std::vector<double>& y, double fs, double f0, double fHi)
{
    const auto margin = static_cast<size_t>(0.25 * fs);
    const std::vector<double> seg(y.begin() + static_cast<std::ptrdiff_t>(margin),
                                  y.end() - static_cast<std::ptrdiff_t>(margin));
    const auto spec = test::rdft(seg);
    const double n = static_cast<double>(seg.size());
    std::vector<bool> harmonic(spec.size(), false);
    for (int k = 1; k * f0 <= 0.5 * fs; ++k)
    {
        const auto bin = static_cast<size_t>(std::lround(k * f0 * n / fs));
        if (bin < harmonic.size())
            harmonic[bin] = true;
    }
    double all = 0.0;
    double other = 0.0;
    for (size_t b = 0; b < spec.size(); ++b)
    {
        const double f = static_cast<double>(b) * fs / n;
        if (f < 20.0 || f > fHi)
            continue;
        const double p = std::norm(spec[b]);
        all += p;
        other += harmonic[b] ? 0.0 : p;
    }
    return 10.0 * std::log10(std::max(other, 1e-300) / all);
}

double rmsOf(const std::vector<double>& y, size_t from, size_t to)
{
    return test::rms(std::span<const double>(y.data() + from, to - from));
}

double maxSecondDifference(const std::vector<double>& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t>(from, 2); i < to; ++i)
        worst = std::max(worst, std::abs(y[i] - 2.0 * y[i - 1] + y[i - 2]));
    return worst;
}
} // namespace

TEST_CASE("Tube at balance -100 has b_eff 0.85", "[drive]")
{
    CHECK(std::abs(effectiveBias(DriveMode::Tube, -100.0) - 0.85) < 1e-15);
    CHECK(effectiveBias(DriveMode::Tube, 0.0) == 0.35);
    CHECK(effectiveBias(DriveMode::Tube, 100.0) == 0.0);
    CHECK(effectiveBias(DriveMode::Soft, 0.0) == 0.0);
    CHECK(effectiveBias(DriveMode::Hard, -100.0) == 0.5);
    // the shapes and their antiderivatives (F1' = f), with the bias: f_b(0) = 0
    for (const DriveMode m : kModes)
        for (const double b : {0.0, 0.35, 0.85})
            for (const double x : {-3.0, -0.7, 0.0, 0.4, 1.3, 2.9})
            {
                INFO(modeName(m) << " b " << b << " x " << x);
                const double h = 1e-6;
                const double slope = (antiderivative(m, x + h, b) - antiderivative(m, x - h, b)) / (2.0 * h);
                CHECK(std::abs(slope - shape(m, x, b)) <= 1e-6);
                CHECK(shape(m, 0.0, b) == 0.0);
            }
}

TEST_CASE("T-DRV-01 harmonics >= -60 dB within 1 dB of the 16x reference (Live, Mix, Print)", "[drive]")
{
    const auto golden = test::Golden::load("drive.sgg");
    for (const double level : {-12.0, 6.0})
    {
        const auto x = sig::sine(55.0, kFs, 2.0, level);
        for (const DriveMode mode : kModes)
        {
            if (level > 0.0 && mode != DriveMode::Fold)
                continue;
            for (const int d : {25, 50, 100})
            {
                const auto ref = golden[std::string("harmonics/") + modeName(mode) + "/" + std::to_string(d) + "/" +
                                        std::to_string(static_cast<int>(level))];
                const double top = *std::max_element(ref.begin(), ref.end());
                for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
                {
                    INFO(modeName(mode) << " " << d << " % at " << level << " dBFS, " << qualityName(q));
                    const auto a = lines(runDrive(x, q, bare(mode, d)), kFs, 55.0, 60);
                    double worst = 0.0;
                    for (size_t k = 0; k < ref.size(); ++k)
                        if (20.0 * std::log10(ref[k] / top) >= -60.0)
                            worst = std::max(worst, std::abs(20.0 * std::log10(a[k] / ref[k])));
                    CHECK(worst <= 1.0);
                }
            }
        }
    }
}

TEST_CASE("T-DRV-02 ADAA lowers non-harmonic power by >= 6 dB vs naive (Hard 100 %, 81 Hz, Live)", "[drive]")
{
    const auto x = sig::sine(81.0, kFs, 1.5, -6.0);
    auto naive = bare(DriveMode::Hard, 100.0);
    naive.adaa = false;
    const double withAdaa = nonharmonicDb(runDrive(x, Quality::Live, bare(DriveMode::Hard, 100.0)), kFs, 81.0, 0.45 * kFs);
    const double without = nonharmonicDb(runDrive(x, Quality::Live, naive), kFs, 81.0, 0.45 * kFs);
    INFO("ADAA " << withAdaa << " dB, naive " << without << " dB (P2: 7.8-9.7 dB apart)");
    CHECK(withAdaa <= without - 6.0);
}

TEST_CASE("T-DRV-03 Auto Gain compensates the shaper within +-0.5 dB: all modes, drive 5-100 %, balance -100/0/100, "
          "f_sub1 30/60/120 Hz",
          "[drive]")
{
    // the table against the prototype's G_c (PHASE2_REPORT Annex A)
    const auto golden = test::Golden::load("drive.sgg");
    const auto& table = AutoGainTable::instance();
    for (const DriveMode mode : kModes)
    {
        const auto gc = golden[std::string("gain/") + modeName(mode)];
        size_t i = 0;
        for (const double b : {-100.0, 0.0, 100.0})
            for (const double d : {5.0, 10.0, 25.0, 50.0, 75.0, 100.0})
            {
                INFO(modeName(mode) << " drive " << d << " balance " << b);
                CHECK(std::abs(table.compensationDb(mode, d, b, 55.0, kFs) - gc[i++]) <= 0.1);
            }
    }
    // the stage without the Tone (F-8: the Tone is not compensated): the RMS of the reference sine as with drive 0
    double worst = 0.0;
    for (const double fSub1 : {30.0, 60.0, 120.0})
    {
        const auto x = sig::sine(fSub1, kFs, 0.6, -12.0);
        const double in = rmsOf(x, 19200, 28800);
        for (const DriveMode mode : kModes)
            for (const double d : {5.0, 10.0, 25.0, 50.0, 75.0, 100.0})
                for (const double b : {-100.0, 0.0, 100.0})
                {
                    DriveParams p;
                    p.drivePct = d;
                    p.mode = mode;
                    p.balance = b;
                    p.tone = false;
                    p.fSub1Hz = fSub1;
                    const auto y = runDrive(x, Quality::Live, p);
                    const double dev = std::abs(20.0 * std::log10(rmsOf(y, 19200, 28800) / in));
                    worst = std::max(worst, dev);
                    if (dev > 0.5)
                    {
                        INFO(modeName(mode) << " drive " << d << " balance " << b << " f_sub1 " << fSub1 << ": " << dev
                                            << " dB");
                        CHECK(dev <= 0.5);
                    }
                }
    }
    INFO("worst deviation " << worst << " dB");
    CHECK(worst <= 0.5);
}

TEST_CASE("Auto Gain leaves the Tone alone (F-8): with the Tone on, the output is the Tone-off output through the Tone",
          "[drive]")
{
    // After the shaper everything is linear (Tone, 5 Hz high-pass, Auto Gain), so with a gain that ignores the Tone the
    // Tone-on output is the Tone-off output through the Tone's low-pass (alpha 1 from 5 % drive). A Tone far below the
    // sub's harmonics, which SPEC 0.4's Auto Gain (D4) chased up to +55 dB, now lowers the level as any tone control.
    for (const double fSub1 : {30.0, 120.0})
        for (const DriveMode mode : {DriveMode::Fold, DriveMode::Hard, DriveMode::Tube})
            for (const double toneHz : {60.0, 500.0})
            {
                INFO(modeName(mode) << " f_sub1 " << fSub1 << " Tone " << toneHz);
                const auto x = sig::sine(fSub1, kFs, 0.6, -12.0);
                DriveParams p;
                p.drivePct = 50.0;
                p.mode = mode;
                p.toneHz = toneHz;
                p.fSub1Hz = fSub1;
                const auto on = runDrive(x, Quality::Live, p);
                p.tone = false;
                auto through = runDrive(x, Quality::Live, p);
                dsp::FilterCascade lp;
                lp.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::LowPass, 2, toneHz, kFs));
                for (auto& v : through)
                    v = lp.process(v);
                double worst = 0.0, peak = 0.0;
                for (size_t i = 9600; i < on.size(); ++i)
                {
                    worst = std::max(worst, std::abs(on[i] - through[i]));
                    peak = std::max(peak, std::abs(on[i]));
                }
                CHECK(worst <= 1e-9 * peak);
            }
}

TEST_CASE("T-DRV-04 DC after the 5 Hz high-pass <= -80 dBFS", "[drive]")
{
    const auto x = sig::sine(55.0, kFs, 3.0, -12.0);
    for (const DriveMode mode : kModes)
        for (const double b : {-100.0, 0.0})
        {
            auto p = bare(mode, 100.0);
            p.balance = b;
            const auto y = runDrive(x, Quality::Live, p);
            double mean = 0.0;
            for (size_t i = 48000; i < y.size(); ++i)
                mean += y[i] / static_cast<double>(y.size() - 48000);
            INFO(modeName(mode) << " balance " << b << ": DC " << mean);
            CHECK(std::abs(mean) <= 1e-4);
        }
}

TEST_CASE("T-DRV-05 drive 0 is bit-exact (delayed by the latency); alpha continuity <= 1e-2 relative per 0.1 % step",
          "[drive]")
{
    const auto x = sig::sine(55.0, kFs, 1.0, -12.0);
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        INFO(qualityName(q));
        DriveParams p;
        p.mode = DriveMode::Fold;
        CHECK(runDrive(x, q, p) == x);
    }
    // the blend region (alpha = drive / 5 up to 5 %): past it alpha is 1 and a step moves the pre-gain only
    for (const DriveMode mode : {DriveMode::Soft, DriveMode::Fold, DriveMode::Hard})
        for (const double d : {0.1, 1.0, 2.5, 4.9})
        {
            INFO(modeName(mode) << " drive " << d);
            const auto a = runDrive(x, Quality::Live, bare(mode, d));
            const auto b = runDrive(x, Quality::Live, bare(mode, d + 0.1));
            std::vector<double> diff(a.size());
            for (size_t i = 0; i < a.size(); ++i)
                diff[i] = b[i] - a[i];
            CHECK(rmsOf(diff, 24000, 48000) / rmsOf(a, 24000, 48000) <= 1e-2);
        }
}

TEST_CASE("T-ALIAS-01 31/81/121 Hz, all five modes, drive 25/50/100 %, inputs -6/0/+6/+12 dBFS, 44.1 and 48 kHz: "
          "non-harmonic power below 500 Hz <= -80 dB (Live) and <= -90 dB (Mix, Print)",
          "[drive]")
{
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        double worstSub = -400.0;
        double worstFull = -400.0; // at -6 dBFS, for the ledger
        for (const double fs : {44100.0, 48000.0})
            for (const double f : {31.0, 81.0, 121.0})
                for (const double level : {-6.0, 0.0, 6.0, 12.0})
                {
                    const auto x = sig::sine(f, fs, 1.5, level);
                    for (const DriveMode mode : kModes)
                        for (const double d : {25.0, 50.0, 100.0})
                        {
                            const auto y = runDrive(x, q, bare(mode, d), fs);
                            const double sub = nonharmonicDb(y, fs, f, 500.0);
                            worstSub = std::max(worstSub, sub);
                            if (level == -6.0)
                                worstFull = std::max(worstFull, nonharmonicDb(y, fs, f, std::min(20000.0, 0.45 * fs)));
                            if (sub > (q == Quality::Live ? -80.0 : -90.0))
                            {
                                INFO(qualityName(q) << " fs " << fs << " " << f << " Hz " << level << " dBFS "
                                                    << modeName(mode) << " " << d << " %: " << sub << " dB");
                                CHECK(sub <= (q == Quality::Live ? -80.0 : -90.0));
                            }
                        }
                }
        UNSCOPED_INFO(qualityName(q) << ": worst below 500 Hz " << worstSub << " dB, full band (-6 dBFS) " << worstFull
                                     << " dB");
        CHECK(worstSub <= (q == Quality::Live ? -80.0 : -90.0));
    }
}

TEST_CASE("Fold up to -6 dBFS is bit-identical to the uncapped fold", "[drive]")
{
    for (const double level : {-24.0, -12.0, -6.0})
    {
        INFO(level << " dBFS");
        const auto x = sig::sine(55.0, kFs, 0.5, level);
        auto capped = bare(DriveMode::Fold, 100.0);
        auto uncapped = capped;
        uncapped.foldCap = false;
        CHECK(runDrive(x, Quality::Mix, capped) == runDrive(x, Quality::Mix, uncapped));
    }
    const auto hot = sig::sine(55.0, kFs, 0.5, 6.0);
    auto uncapped = bare(DriveMode::Fold, 100.0);
    uncapped.foldCap = false;
    CHECK(runDrive(hot, Quality::Mix, bare(DriveMode::Fold, 100.0)) != runDrive(hot, Quality::Mix, uncapped));
}

TEST_CASE("AutoGainTable is computed once and process() does not allocate", "[drive]")
{
    const auto x = sig::sine(55.0, kFs, 0.5, -12.0);
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        Drive drive;
        drive.prepare(kFs, 256, q);
        std::vector<double> buf(x);
        size_t allocations = 0;
        for (size_t i = 0; i + 256 <= buf.size(); i += 256)
        {
            DriveParams p; // parameters move every block, including the mode and drive 0
            p.drivePct = static_cast<double>((i / 256) % 7) * 15.0;
            p.mode = kModes[(i / 256) % 5];
            p.balance = (i / 256) % 2 == 0 ? -50.0 : 50.0;
            p.toneHz = 200.0 + static_cast<double>(i % 1000);
            test::AllocGuard guard;
            drive.setParams(p);
            drive.process(std::span<double>(buf.data() + i, 256));
            allocations += guard.count();
        }
        CHECK(allocations == 0);
        CHECK(std::all_of(buf.begin(), buf.end(), [](double v) { return std::isfinite(v); }));
    }
    CHECK(AutoGainTable::computations() == 1);
}

TEST_CASE("drive, mode and on/off changes are smoothed and crossfaded without clicks", "[drive]")
{
    // a step of any parameter at 0.25 s: the largest second difference after it stays near the steady one
    const auto x = sig::sine(55.0, kFs, 0.5, -12.0);
    struct Change
    {
        const char* name;
        DriveParams before;
        DriveParams after;
    };
    DriveParams soft20;
    soft20.drivePct = 20.0;
    auto soft80 = soft20;
    soft80.drivePct = 80.0;
    auto soft60 = soft20;
    soft60.drivePct = 60.0;
    auto fold60 = soft60;
    fold60.mode = DriveMode::Fold; // waveforms far apart: a cut would show
    DriveParams off;
    const Change changes[] = {{"drive 20 -> 80 %", soft20, soft80},
                              {"Soft -> Fold at 60 %", soft60, fold60},
                              {"off -> 20 %", off, soft20},
                              {"20 % -> off", soft20, off}};
    for (const auto& c : changes)
        for (const Quality q : {Quality::Live, Quality::Mix})
        {
            INFO(c.name << ", " << qualityName(q));
            Drive drive;
            drive.prepare(kFs, 64, q);
            drive.setParams(c.before);
            std::vector<double> y(x);
            for (size_t i = 0; i < y.size(); i += 64)
            {
                if (i == 12000)
                    drive.setParams(c.after);
                drive.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
            }
            const double steady = std::max(maxSecondDifference(y, 6000, 12000), maxSecondDifference(y, 18000, 24000));
            const double change = maxSecondDifference(y, 12000, 18000);
            INFO("steady " << steady << ", around the change " << change);
            CHECK(change <= 1.5 * steady + 1e-4);
        }
}

TEST_CASE("Drive latency: Live 0, Mix and Print the oversamplers' (integer)", "[drive]")
{
    for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
        {
            Drive drive;
            drive.prepare(fs, 512, q);
            UNSCOPED_INFO("fs " << fs << " " << qualityName(q) << ": L_os " << drive.latencySamples() << " samples");
            if (q == Quality::Live)
                CHECK(drive.latencySamples() == 0);
            else
                CHECK(drive.latencySamples() > 0);
        }
    CHECK(true);
}
