// SPDX-License-Identifier: AGPL-3.0-or-later
// The sub path's compressor and gate (SPEC §7.4, §7.5, T-CMP-01..03, T-GATE-01): ripple-free detectors over
// max(10 ms, 1.5 / f_sub1), the static curve with its knee, gain smoothing in dB, auto release, the gate's hysteresis
// and hold, against the prototype (tests/golden/dynamics.sgg, proto/subgen_proto/dynamics.py).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

#include "chain/Compressor.h"
#include "chain/Gate.h"
#include "chain/LevelDetector.h"
#include "core/Quality.h"
#include "dsp/Fft.h"
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

std::vector<double> concat(std::initializer_list<std::vector<double>> parts)
{
    std::vector<double> out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

CompParams comp(double threshDb, double ratio, double kneeDb, double attackMs, double releaseMs, Detector detector,
                double fSub1Hz, bool autoRelease = false)
{
    CompParams p;
    p.on = true;
    p.threshDb = threshDb;
    p.ratio = ratio;
    p.kneeDb = kneeDb;
    p.attackMs = attackMs;
    p.releaseMs = releaseMs;
    p.detector = detector;
    p.fSub1Hz = fSub1Hz;
    p.autoRelease = autoRelease;
    return p;
}

struct CompRun
{
    std::vector<double> out; // aligned with the input
    std::vector<double> gr;  // dB, per input sample
};

CompRun runComp(const std::vector<double>& x, const CompParams& p, Quality q = Quality::Live)
{
    Compressor c;
    c.prepare(kFs, 1, q);
    c.setParams(p);
    const auto latency = static_cast<size_t>(c.latencySamples());
    CompRun r;
    std::vector<double> raw(x.size() + latency);
    r.gr.resize(x.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        raw[i] = i < x.size() ? x[i] : 0.0;
        c.process(std::span<double>(&raw[i], 1));
        if (i < x.size())
            r.gr[i] = c.gainReductionDb();
    }
    r.out.assign(raw.begin() + static_cast<std::ptrdiff_t>(latency), raw.end());
    return r;
}

GateParams gate(double threshDb, double rangeDb, double holdMs, double releaseMs, double fSub1Hz)
{
    GateParams p;
    p.on = true;
    p.threshDb = threshDb;
    p.rangeDb = rangeDb;
    p.holdMs = holdMs;
    p.releaseMs = releaseMs;
    p.fSub1Hz = fSub1Hz;
    return p;
}

struct GateRun
{
    std::vector<double> out;
    std::vector<double> gainDb;
    std::vector<bool> open;
};

GateRun runGate(const std::vector<double>& x, const GateParams& p)
{
    Gate g;
    g.prepare(kFs, 1);
    g.setParams(p);
    GateRun r;
    r.out = x;
    for (size_t i = 0; i < x.size(); ++i)
    {
        g.process(std::span<double>(&r.out[i], 1));
        r.gainDb.push_back(g.gainDb());
        r.open.push_back(g.isOpen());
    }
    return r;
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.size() % 2 == 1 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
}

// Energy above 100 Hz in the first 80 ms (Hann, zero-padded to 2^16) of a 40 Hz -6 dBFS tone after silence that drives
// the compressor from 0 to ~18 dB of reduction (-30 dBFS, 4:1).
double onsetSplatterDb(Detector detector, double attackMs)
{
    const auto x = concat({std::vector<double>(24000, 0.0), sig::sine(40.0, kFs, 1.0, -6.0)});
    const auto y = runComp(x, comp(-30.0, 4.0, 0.0, attackMs, 100.0, detector, 40.0)).out;
    const size_t len = static_cast<size_t>(0.08 * kFs);
    std::vector<double> seg(1 << 16, 0.0);
    for (size_t i = 0; i < len; ++i) // numpy.hanning
        seg[i] = y[24000 + i] * (0.5 + 0.5 * std::cos(std::numbers::pi * (2.0 * static_cast<double>(i) + 1.0 -
                                                                         static_cast<double>(len)) /
                                                      static_cast<double>(len - 1)));
    dsp::RealFft fft(16);
    std::vector<std::complex<double>> spec((1 << 16) / 2 + 1);
    fft.forward(seg.data(), spec.data());
    double all = 0.0;
    double above = 0.0;
    for (size_t b = 0; b < spec.size(); ++b)
    {
        const double p = std::norm(spec[b]);
        all += p;
        above += static_cast<double>(b) * kFs / (1 << 16) > 100.0 ? p : 0.0;
    }
    return 10.0 * std::log10(above / all);
}
} // namespace

TEST_CASE("T-CMP-01 static curve and knee within 0.1 dB", "[dynamics]")
{
    // L for 2(L - T) < -W; L + (1/R - 1)(L - T + W/2)^2 / (2W) inside the knee; T + (L - T)/R above it; GR = out - L
    auto formula = [](double l, double t, double r, double w) {
        const double d = l - t;
        double out = l;
        if (w <= 0.0)
            out = d > 0.0 ? t + d / r : l;
        else if (2.0 * d > w)
            out = t + d / r;
        else if (2.0 * d >= -w)
            out = l + (1.0 / r - 1.0) * (d + w / 2.0) * (d + w / 2.0) / (2.0 * w);
        return out - l;
    };
    for (const double t : {-30.0, -18.0, -6.0})
        for (const double r : {1.5, 3.0, 20.0})
            for (const double w : {0.0, 6.0, 12.0})
            {
                INFO("T " << t << " R " << r << " W " << w);
                double previous = staticGainReductionDb(-80.0, t, r, w);
                for (double l = -80.0; l <= 0.0; l += 0.01)
                {
                    const double gr = staticGainReductionDb(l, t, r, w);
                    CHECK(std::abs(gr - formula(l, t, r, w)) <= 1e-12);
                    CHECK(std::abs(gr - previous) <= 0.01); // continuous through the knee
                    previous = gr;
                }
            }
    // the stage on a steady 200 Hz tone (Peak reads the peak): the measured reduction is the curve's within 0.1 dB
    for (const double level : {-24.0, -18.0, -12.0, -6.0})
    {
        INFO(level << " dBFS");
        const auto r = runComp(sig::sine(200.0, kFs, 1.0, level), comp(-18.0, 3.0, 6.0, 5.0, 50.0, Detector::Peak, 200.0));
        CHECK(std::abs(r.gr[40000] - staticGainReductionDb(level, -18.0, 3.0, 6.0)) <= 0.1);
    }
}

TEST_CASE("T-CMP-02 THD at 6 dB GR <= -60 dB for RMS and Peak (30/40/60/100 Hz x attack 0.1-100 ms x release "
          "10-1000 ms)",
          "[dynamics]")
{
    // -12 dBFS: Peak reads -12 dB, RMS -15.01 dB; the threshold 8 dB below at 4:1 gives 6 dB of reduction
    for (const double f : {30.0, 40.0, 60.0, 100.0})
    {
        const auto x = sig::sine(f, kFs, 3.0, -12.0);
        for (const Detector d : {Detector::Peak, Detector::Rms})
        {
            const double level = d == Detector::Peak ? -12.0 : -12.0 + 20.0 * std::log10(std::sqrt(0.5));
            for (const double attack : {0.1, 1.0, 10.0, 100.0})
                for (const double release : {10.0, 100.0, 1000.0})
                {
                    INFO(f << " Hz, " << (d == Detector::Peak ? "Peak" : "RMS") << ", attack " << attack << ", release "
                           << release);
                    const auto r = runComp(x, comp(level - 8.0, 4.0, 0.0, attack, release, d, f));
                    CHECK(std::abs(median({r.gr.begin() + 48000, r.gr.begin() + 96000}) + 6.0) <= 0.05);
                    CHECK(test::thdDb(std::span<const double>(r.out.data() + 48000, 48000), kFs, f) <= -60.0);
                }
        }
    }
}

TEST_CASE("T-CMP-02 onset: 40 Hz -6 dBFS after silence, threshold -30 dB, ratio 4 (GR 0 -> ~18 dB), energy above 100 "
          "Hz in the first 80 ms (Hann): Peak at attack 0.1/1/3 ms <= -50 dB; RMS at 1 ms <= -40 dB and equal to RMS at "
          "6.25 ms",
          "[dynamics]")
{
    for (const double attack : {0.1, 1.0, 3.0})
    {
        INFO("Peak, attack " << attack << " ms");
        CHECK(onsetSplatterDb(Detector::Peak, attack) <= -50.0);
    }
    const double rms1 = onsetSplatterDb(Detector::Rms, 1.0);
    CHECK(rms1 <= -40.0);
    CHECK(std::abs(rms1 - onsetSplatterDb(Detector::Rms, 6.25)) <= 1e-9); // the attack floor is 0.25 T_sub = 6.25 ms
}

TEST_CASE("T-CMP-03 attack and release 63 % times within 10 % with the detector window added to the release; auto "
          "release after 0.05/0.5/3 s of reduction = 64/498/514 ms +- 10 %",
          "[dynamics]")
{
    // a 200 Hz tone steps by 30 dB and back (window 10 ms)
    const auto x = concat({sig::sine(200.0, kFs, 1.0, -40.0), sig::sine(200.0, kFs, 1.0, -10.0), sig::sine(200.0, kFs, 1.0, -40.0)});
    for (const auto& [attack, release] : {std::pair{20.0, 100.0}, std::pair{50.0, 300.0}})
    {
        INFO("attack " << attack << ", release " << release);
        const auto gr = runComp(x, comp(-30.0, 4.0, 0.0, attack, release, Detector::Peak, 200.0)).gr;
        const double lo = gr[static_cast<size_t>(0.9 * kFs)];
        const double hi = gr[static_cast<size_t>(1.9 * kFs)];
        size_t a = 48000;
        while (!(gr[a] <= lo + 0.632 * (hi - lo)))
            ++a;
        size_t b = 96000;
        while (!(gr[b] >= hi + 0.632 * (lo - hi)))
            ++b;
        CHECK(std::abs(static_cast<double>(a - 48000) / kFs * 1000.0 / attack - 1.0) < 0.10);
        CHECK(std::abs(static_cast<double>(b - 96000) / kFs * 1000.0 / (release + 10.0) - 1.0) < 0.10);
    }
    // auto release: the deeper of (attack, 50 ms) and (150 ms, 500 ms); P2: 64 / 498 / 514 ms
    for (const auto& [burst, expected] : {std::pair{0.05, 64.0}, std::pair{0.5, 498.0}, std::pair{3.0, 514.0}})
    {
        INFO("burst " << burst << " s");
        const auto y = concat({sig::sine(100.0, kFs, burst, -6.0), sig::sine(100.0, kFs, 2.0, -40.0)});
        const auto gr = runComp(y, comp(-30.0, 4.0, 0.0, 5.0, 50.0, Detector::Peak, 100.0, true)).gr;
        const auto n0 = static_cast<size_t>(std::lround(burst * kFs));
        const double depth = gr[n0 - 1];
        size_t k = n0;
        while (!(gr[k] >= depth * std::exp(-1.0)))
            ++k;
        const double ms = static_cast<double>(k - n0) / kFs * 1000.0;
        INFO("release " << ms << " ms");
        CHECK(std::abs(ms / expected - 1.0) <= 0.10);
    }
}

TEST_CASE("T-GATE-01 threshold, hold, release and hysteresis within 10 %, no chatter at threshold +- 1 dB",
          "[dynamics]")
{
    {
        // a 100 Hz tone swinging +-1 dB around the threshold (2 Hz): with 3 dB of hysteresis it opens once
        std::vector<double> x(3 * 48000);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double>(i) / kFs;
            x[i] = std::pow(10.0, (-50.0 + std::sin(2.0 * std::numbers::pi * 2.0 * t)) / 20.0) *
                   std::sin(2.0 * std::numbers::pi * 100.0 * t);
        }
        const auto r = runGate(x, gate(-50.0, -40.0, 40.0, 120.0, 100.0));
        int changes = 0;
        for (size_t i = 1; i < r.open.size(); ++i)
            changes += r.open[i] != r.open[i - 1] ? 1 : 0;
        CHECK(changes <= 2);
    }
    {
        // |x| = 0.1 for 0.5 s, then silence: it closes after the 15 ms window plus the 40 ms hold, and the gain falls
        // by 63 % of the range within the release
        auto x = std::vector<double>(24000, 0.1);
        x.resize(72000, 0.0);
        const auto r = runGate(x, gate(-30.0, -40.0, 40.0, 120.0, 100.0));
        CHECK(r.open[12000]);
        size_t close = 1;
        while (close < r.open.size() && !(r.open[close - 1] && !r.open[close]))
            ++close;
        const auto want = static_cast<size_t>(24000 + std::lround(0.015 * kFs) + std::lround(0.040 * kFs));
        CHECK(std::abs(static_cast<double>(close) - static_cast<double>(want)) <= 2.0);
        size_t k = close;
        while (k < r.gainDb.size() && !(r.gainDb[k] <= -40.0 * (1.0 - std::exp(-1.0))))
            ++k;
        CHECK(std::abs(static_cast<double>(k - close) / kFs * 1000.0 / 120.0 - 1.0) < 0.10);
    }
    for (const auto& [level, opens] : {std::pair{-29.5, true}, std::pair{-30.5, false}})
    {
        INFO(level << " dBFS");
        const auto r = runGate(sig::sine(100.0, kFs, 1.0, level), gate(-30.0, -40.0, 40.0, 120.0, 100.0));
        CHECK(std::any_of(r.open.begin() + 24000, r.open.end(), [](bool o) { return o; }) == opens);
    }
    {
        const auto x = sig::sine(100.0, kFs, 2.0, -60.0); // below the threshold all along: the range
        const auto r = runGate(x, gate(-50.0, -40.0, 40.0, 120.0, 100.0));
        CHECK(std::abs(20.0 * std::log10(test::rms(std::span<const double>(r.out.data() + 48000, 48000)) /
                                         test::rms(std::span<const double>(x.data() + 48000, 48000))) +
                       40.0) <= 0.01);
    }
}

TEST_CASE("off is bit-exact", "[dynamics]")
{
    const auto x = sig::kick(kFs, {.nHits = 2, .spacing = 0.25, .levelDb = -3.0});
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        INFO("quality " << static_cast<int>(q));
        auto p = comp(-40.0, 10.0, 0.0, 1.0, 50.0, Detector::Peak, 45.0);
        p.on = false;
        CHECK(runComp(x, p, q).out == x); // delayed by the latency (Print: 5 ms), nothing else
    }
    auto g = gate(-10.0, -40.0, 40.0, 120.0, 45.0);
    g.on = false;
    CHECK(runGate(x, g).out == x);
}

TEST_CASE("Print looks 5 ms ahead; switching on and off crossfades over 10 ms without clicks", "[dynamics]")
{
    Compressor c;
    c.prepare(kFs, 64, Quality::Print);
    CHECK(c.latencySamples() == 240);
    c.prepare(kFs, 64, Quality::Mix);
    CHECK(c.latencySamples() == 0);
    // a sustained 55 Hz tone deep in compression: the switch blends the two outputs
    const auto x = sig::sine(55.0, kFs, 0.6, -6.0);
    for (const bool toOn : {true, false})
    {
        INFO((toOn ? "off -> on" : "on -> off"));
        auto p = comp(-30.0, 8.0, 0.0, 2.0, 80.0, Detector::Peak, 55.0);
        p.on = !toOn;
        Compressor cp;
        cp.prepare(kFs, 64, Quality::Mix);
        cp.setParams(p);
        std::vector<double> y(x);
        for (size_t i = 0; i < y.size(); i += 64)
        {
            if (i == 14400)
            {
                p.on = toOn;
                cp.setParams(p);
            }
            cp.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
        }
        double steady = 0.0;
        double change = 0.0;
        for (size_t i = 9600; i < 24000; ++i)
        {
            const double d2 = std::abs(y[i] - 2.0 * y[i - 1] + y[i - 2]);
            (i >= 14400 && i < 14400 + 960 ? change : steady) = std::max(i >= 14400 && i < 14400 + 960 ? change : steady, d2);
        }
        INFO("steady " << steady << ", during the switch " << change);
        CHECK(change <= 1.5 * steady + 1e-4);
    }
}

TEST_CASE("the dynamics do not allocate and do not depend on the block size", "[dynamics]")
{
    const auto x = concat({sig::sine(55.0, kFs, 0.3, -6.0), sig::sine(55.0, kFs, 0.3, -50.0)});
    auto run = [&](size_t block) {
        Compressor c;
        Gate g;
        c.prepare(kFs, static_cast<int>(block), Quality::Print);
        g.prepare(kFs, static_cast<int>(block));
        auto cp = comp(-30.0, 4.0, 6.0, 5.0, 50.0, Detector::Rms, 55.0, true);
        cp.autoMakeup = true;
        c.setParams(cp);
        g.setParams(gate(-40.0, -30.0, 20.0, 80.0, 55.0));
        std::vector<double> y(x);
        size_t allocations = 0;
        for (size_t i = 0; i < y.size(); i += block)
        {
            const auto s = std::span<double>(y.data() + i, std::min(block, y.size() - i));
            test::AllocGuard guard;
            g.process(s);
            c.process(s);
            allocations += guard.count();
        }
        CHECK(allocations == 0);
        return y;
    };
    const auto a = run(1);
    CHECK(run(64) == a);
    CHECK(run(997) == a);
}

TEST_CASE("golden GR trajectories within 1e-9", "[dynamics]")
{
    const auto golden = test::Golden::load("dynamics.sgg");
    const auto onset = concat({std::vector<double>(24000, 0.0), sig::sine(40.0, kFs, 0.4, -6.0)});
    const auto drop = concat({sig::sine(100.0, kFs, 0.3, -6.0), sig::sine(100.0, kFs, 0.5, -40.0)});
    const auto knee = concat({sig::sine(55.0, kFs, 0.2, -30.0), sig::sine(55.0, kFs, 0.2, -20.0), sig::sine(55.0, kFs, 0.2, -10.0)});
    const auto gateInput = concat({sig::sine(100.0, kFs, 0.3, -20.0), sig::sine(100.0, kFs, 0.4, -60.0)});
    auto compare = [](const std::vector<double>& got, std::span<const double> want) {
        REQUIRE((got.size() + 31) / 32 == want.size());
        double worst = 0.0;
        for (size_t i = 0; i < want.size(); ++i)
            worst = std::max(worst, std::abs(got[32 * i] - want[i]));
        return worst;
    };
    CHECK(compare(runComp(onset, comp(-30.0, 4.0, 0.0, 1.0, 100.0, Detector::Peak, 40.0)).gr, golden["gr/peak_onset"]) <= 1e-9);
    CHECK(compare(runComp(drop, comp(-30.0, 4.0, 6.0, 5.0, 50.0, Detector::Rms, 100.0, true)).gr, golden["gr/rms_autorel"]) <= 1e-9);
    CHECK(compare(runComp(knee, comp(-20.0, 3.0, 6.0, 10.0, 150.0, Detector::Peak, 55.0)).gr, golden["gr/peak_knee"]) <= 1e-9);
    CHECK(compare(runGate(gateInput, gate(-30.0, -40.0, 40.0, 120.0, 100.0)).gainDb, golden["gain/gate"]) <= 1e-9);
}
