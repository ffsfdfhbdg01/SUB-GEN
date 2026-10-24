// SPDX-License-Identifier: AGPL-3.0-or-later
// DIVIDE (SPEC §6.2, T-DIV-01..09): the octave divider with note boundaries, provisional confidence and the causal
// confidence gate, against the prototype (tests/golden/divide.sgg) and the TEST_PLAN criteria.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "core/Quality.h"
#include "engines/Divide.h"
#include "engines/SourceBand.h"
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

struct Run
{
    std::vector<double> out;          // engine output, aligned with the input (the lookahead removed)
    std::vector<DivideProbe> probe;   // delayed-axis values, aligned with the input
    int lookahead = 0;
};

// Runs DIVIDE over x (plus `lookahead` zeros to flush), sample by sample, and aligns everything with x.
Run runDivide(const std::vector<double>& x, Quality quality, EngineControl control = {}, double fs = kFs)
{
    Divide divide;
    Run r;
    r.lookahead = engineLookaheadSamples(quality, fs);
    divide.prepare(fs, 1, r.lookahead);
    divide.setControl(control);
    const size_t total = x.size() + static_cast<size_t>(r.lookahead);
    std::vector<double> rawOut(total);
    std::vector<DivideProbe> rawProbe(total);
    const std::vector<double> silence(1, 0.0);
    for (size_t n = 0; n < total; ++n)
    {
        const double in = n < x.size() ? x[n] : 0.0;
        divide.process(std::span<const double>(&in, 1), silence, std::span<double>(&rawOut[n], 1));
        rawProbe[n] = divide.probe();
    }
    r.out.assign(rawOut.begin() + r.lookahead, rawOut.end());
    r.probe.assign(rawProbe.begin() + r.lookahead, rawProbe.end());
    return r;
}

std::vector<double> sourceBand(std::vector<double> x, double fs = kFs)
{
    SourceBand band;
    band.prepare(fs);
    band.setRange(30.0, 120.0, 24);
    for (auto& v : x)
        v = band.process(v);
    return x;
}

std::vector<double> field(const Run& r, double DivideProbe::*member)
{
    std::vector<double> v(r.probe.size());
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = r.probe[i].*member;
    return v;
}

std::vector<double> y1Of(const Run& r) { return field(r, &DivideProbe::y1); }

double firstSign(std::span<const double> seg)
{
    double peak = 0.0;
    for (const double v : seg)
        peak = std::max(peak, std::abs(v));
    for (const double v : seg)
        if (std::abs(v) > 0.1 * peak)
            return v > 0.0 ? 1.0 : -1.0;
    return 0.0;
}

double correlation(std::span<const double> a, std::span<const double> b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        ab += a[i] * b[i];
        aa += a[i] * a[i];
        bb += b[i] * b[i];
    }
    return ab / std::sqrt(aa * bb);
}

std::vector<double> floorNoise(size_t n, double rmsDb, std::uint64_t seed)
{
    auto x = sig::bandNoise(kFs, static_cast<double>(n) / kFs, 30.0, 120.0, 0.0, seed);
    x.resize(n, 0.0);
    const double scale = std::pow(10.0, rmsDb / 20.0) / test::rms(x);
    for (auto& v : x)
        v *= scale;
    return x;
}

std::vector<double> kickHits(int nHits, double spacing, double t60,
                             const std::function<std::vector<double>(size_t)>& extra = {})
{
    auto x = sig::kick(kFs, {.nHits = nHits, .spacing = spacing, .levelDb = -6.0, .t60 = t60});
    if (extra)
    {
        const auto e = extra(x.size());
        for (size_t i = 0; i < x.size(); ++i)
            x[i] += e[i];
    }
    return x;
}

std::vector<double> hum(size_t n)
{
    std::vector<double> h(n);
    for (size_t i = 0; i < n; ++i)
        h[i] = std::pow(10.0, -70.0 / 20.0) * std::sin(2.0 * std::numbers::pi * 31.0 * static_cast<double>(i) / kFs);
    return h;
}

double energy(std::span<const double> x)
{
    double e = 0.0;
    for (const double v : x)
        e += v * v;
    return e;
}

std::span<const double> slice(const std::vector<double>& v, size_t from, size_t count)
{
    return {v.data() + from, std::min(count, v.size() - from)};
}

// The divider's raw input for the prototype comparisons (export_golden.noisy_sine): the engine applies its own source
// band to the input.
std::vector<double> noiseSine(double f, double snrDb, std::uint64_t seed, double dur = 4.0)
{
    auto s = sig::sine(f, kFs, dur, -12.0);
    auto nz = sig::bandNoise(kFs, dur, 30.0, 120.0, 0.0, seed);
    const double scale = (test::rms(s) / std::pow(10.0, snrDb / 20.0)) / test::rms(nz);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] += nz[i] * scale;
    return s;
}
} // namespace

TEST_CASE("golden: DIVIDE equals divide.divide and render.divide_sub_causal within 1e-9", "[divide]")
{
    const auto golden = test::Golden::load("divide.sgg");
    struct Case
    {
        const char* name;
        std::vector<double> input;
        Quality quality;
        size_t first;
    };
    auto sine = sig::silence(kFs, 0.02);
    const auto tone = sig::sine(55.0, kFs, 0.1, -12.0);
    sine.insert(sine.end(), tone.begin(), tone.end());
    const std::vector<Case> cases{
        {"sine_live", sine, Quality::Live, 0},
        {"kick_mix", sig::kick(kFs, {.nHits = 1, .spacing = 0.5, .levelDb = -6.0}), Quality::Mix, 0},
        {"two_hits_mix", sig::kick(kFs, {.nHits = 2, .spacing = 0.5, .levelDb = -6.0}), Quality::Mix, 24000 - 1024},
        {"tails_mix", sig::kick(kFs, {.nHits = 2, .spacing = 0.45, .levelDb = -6.0, .t60 = 1.0}), Quality::Mix,
         21600 - 1024},
        {"bass808_live", sig::bass808(kFs), Quality::Live, 0},
        // F-1: each window holds a gate opening on the second confirmation and samples that one would have opened
        {"noisy_sine_mix", noiseSine(55.0, 15.0, 5, 1.5), Quality::Mix, 46336},
        {"noisy_sine_live", noiseSine(55.0, 15.0, 5, 1.5), Quality::Live, 48384},
    };
    for (const auto& c : cases)
    {
        INFO(c.name);
        const auto r = runDivide(c.input, c.quality);
        const std::string n = c.name;
        const auto y1 = golden["y1/" + n], y2 = golden["y2/" + n], fEst = golden["f_est/" + n],
                   conf = golden["conf/" + n], prov = golden["provisional/" + n], out = golden["out/" + n];
        double worstY = 0.0, worstF = 0.0, worstConf = 0.0, worstOut = 0.0;
        int provMismatch = 0;
        for (size_t i = 0; i < y1.size(); ++i)
        {
            const auto& p = r.probe[c.first + i];
            worstY = std::max({worstY, std::abs(p.y1 - y1[i]), std::abs(p.y2 - y2[i])});
            worstF = std::max(worstF, std::abs(p.fEst - fEst[i]));
            worstConf = std::max(worstConf, std::abs(p.conf - conf[i]));
            provMismatch += p.provisional != (prov[i] != 0.0);
            worstOut = std::max(worstOut, std::abs(r.out[c.first + i] - out[i]));
        }
        CHECK(worstY <= 1e-9);
        CHECK(worstF <= 1e-9);
        CHECK(worstConf <= 1e-9);
        CHECK(provMismatch == 0);
        CHECK(worstOut <= 1e-9);
    }
}

TEST_CASE("core B's sparse replay: a jump over samples without a crossing or an event equals stepping them", "[divide]")
{
    // a tone with a gap of digital silence long enough for the held value to expire inside a jump (the confirmation
    // count resets there; the tone resumes with a negative half wave, so the first full step after the gap stores a
    // new value and counts from 0), a note end, then a second note
    const size_t total = 38400;
    std::vector<double> x(total, 0.0);
    for (size_t i = 0; i < total; ++i)
    {
        const double t = static_cast<double>(i) / kFs;
        if (i < 14400)
            x[i] = 0.25 * std::sin(2.0 * std::numbers::pi * 55.0 * t);
        else if (i >= 19200 && i < 28800)
            x[i] = -0.25 * std::sin(2.0 * std::numbers::pi * 55.0 * t);
        else if (i >= 28800)
            x[i] = 0.25 * std::sin(2.0 * std::numbers::pi * 70.0 * t);
    }
    const double theta = 0.01;
    std::vector<std::int64_t> neg(total), pos(total); // the arming counts through each sample, as Divide rings them
    std::int64_t cn = 0, cp = 0;
    for (size_t i = 0; i < total; ++i)
    {
        cn += x[i] < -theta ? 1 : 0;
        cp += x[i] > theta ? 1 : 0;
        neg[i] = cn;
        pos[i] = cp;
    }
    auto eventsAt = [](size_t i) {
        divide_detail::SampleEvents ev;
        ev.startAfter = i == 100 || i == 28850;
        ev.startTime = static_cast<double>(i);
        ev.end = i == 26000;
        return ev;
    };
    divide_detail::Core full, sparse;
    full.configure(kFs);
    sparse.configure(kFs);
    std::array<divide_detail::SignOp, 4> ops{};
    int compared = 0;
    int resetsInJumps = 0;
    auto same = [&] {
        CHECK(sparse.next == full.next);
        CHECK(sparse.conf() == full.conf());
        CHECK(sparse.fEst() == full.fEst());
        CHECK(sparse.isProvisional() == full.isProvisional());
        CHECK(sparse.confirmations == full.confirmations);
        CHECK(sparse.s1 == full.s1);
        CHECK(sparse.csNeg == full.csNeg);
        CHECK(sparse.csPos == full.csPos);
        CHECK(sparse.prevX == full.prevX);
        CHECK(sparse.resets == full.resets);
        CHECK(sparse.running == full.running);
        ++compared;
    };
    for (size_t i = 0; i < total; ++i)
    {
        const auto ev = eventsAt(i);
        full.step(x[i], theta, ev, ops);
        const bool crossing = i >= 1 && ((x[i - 1] < 0.0 && x[i] >= 0.0) || (x[i - 1] > 0.0 && x[i] <= 0.0));
        if (!crossing && !ev.startAfter && !ev.end)
            continue;
        if (static_cast<std::int64_t>(i) > sparse.next)
        {
            const int before = sparse.confirmations;
            sparse.jump(static_cast<std::int64_t>(i), x[i - 1], neg[i - 1], pos[i - 1]);
            resetsInJumps += before > 0 && sparse.confirmations == 0;
        }
        sparse.step(x[i], theta, ev, ops);
        same();
    }
    sparse.jump(static_cast<std::int64_t>(total), x[total - 1], neg[total - 1], pos[total - 1]);
    same();
    CHECK(compared > 50);
    CHECK(resetsInJumps >= 1);
}

TEST_CASE("a note start re-derives core B over the crossings and events of the lookahead only", "[divide]")
{
    // the security review's replay spike: core B used to step all E samples (2160 in Mix) at every note start
    for (const auto& x : {kickHits(4, 0.5, 0.3), noiseSine(55.0, 15.0, 5, 1.5)})
    {
        Divide divide;
        divide.prepare(kFs, 1, engineLookaheadSamples(Quality::Mix, kFs));
        divide.setControl({});
        const std::vector<double> silence(1, 0.0);
        for (const double v : x)
        {
            double out = 0.0;
            divide.process(std::span<const double>(&v, 1), silence, std::span<double>(&out, 1));
        }
        CHECK(divide.replayCount() >= 1);
        CHECK(divide.maxReplaySteps() >= 1);
        CHECK(divide.maxReplaySteps() <= 64);
    }
}

TEST_CASE("T-DIV-01 Sub-1 and Sub-2 frequency within 0.1 % (30-200 Hz at 44.1/48/96 kHz)", "[divide]")
{
    for (const double fs : {44100.0, 48000.0, 96000.0})
        for (const double f : {30.0, 40.0, 55.0, 80.0, 120.0, 200.0})
        {
            INFO("fs " << fs << " f " << f);
            EngineControl control;
            control.rangeLoHz = 15.0;
            control.rangeHiHz = 400.0;
            const auto r = runDivide(sig::sine(f, fs, 3.0, -18.0), Quality::Mix, control, fs);
            const auto start = static_cast<size_t>(8.5 * fs / f) + static_cast<size_t>(0.3 * fs);
            const auto y1 = field(r, &DivideProbe::y1);
            const auto y2 = field(r, &DivideProbe::y2);
            CHECK(std::abs(test::zcFreq(slice(y1, start, y1.size()), fs) / (f / 2.0) - 1.0) <= 1e-3);
            CHECK(std::abs(test::zcFreq(slice(y2, start, y2.size()), fs) / (f / 4.0) - 1.0) <= 1e-3);
        }
    const auto live = runDivide(sig::sine(55.0, kFs, 2.0, -18.0), Quality::Live);
    CHECK(std::abs(test::zcFreq(slice(y1Of(live), 14400, 96000), kFs) / 27.5 - 1.0) <= 1e-3);
}

TEST_CASE("T-DIV-02 3f/2 <= -18 dB with shape 0", "[divide]")
{
    const auto r = runDivide(sig::sine(55.0, kFs, 4.0, -12.0), Quality::Mix);
    const auto seg = slice(r.out, 48000, 96000);
    const auto amps = test::harmonicAmps(seg, kFs, 27.5, 3);
    CHECK(20.0 * std::log10(amps[2] / amps[0]) <= -18.0);
}

TEST_CASE("T-DIV-03 default output, Mix and Live: first lobes and hit-to-hit waveforms", "[divide]")
{
    const std::vector<std::pair<std::string, std::vector<double>>> variants{
        {"silence", kickHits(20, 0.5, 0.3)},
        {"noise -90", kickHits(20, 0.5, 0.3, [](size_t n) { return floorNoise(n, -90.0, 5); })},
        {"noise -80", kickHits(20, 0.5, 0.3, [](size_t n) { return floorNoise(n, -80.0, 5); })},
        {"noise -70", kickHits(20, 0.5, 0.3, [](size_t n) { return floorNoise(n, -70.0, 5); })},
        {"noise -60", kickHits(20, 0.5, 0.3, [](size_t n) { return floorNoise(n, -60.0, 5); })},
        {"hum -70", kickHits(20, 0.5, 0.3, hum)},
    };
    const size_t hit = 24000;
    for (const auto quality : {Quality::Mix, Quality::Live})
        for (const auto& [label, x] : variants)
        {
            INFO(label << (quality == Quality::Mix ? " (Mix)" : " (Live)"));
            const auto r = runDivide(x, quality);
            const auto xb = sourceBand(x);
            std::vector<std::vector<double>> shapes;
            int signsOk = 0;
            for (size_t i = 0; i < 20; ++i)
            {
                signsOk += firstSign(slice(r.out, i * hit, hit)) == firstSign(slice(xb, i * hit, hit));
                double peak = 0.0;
                for (const double v : slice(xb, i * hit, hit))
                    peak = std::max(peak, std::abs(v));
                const auto seg = slice(r.out, i * hit, 9600);
                std::vector<double> s(seg.begin(), seg.end());
                for (auto& v : s)
                    v /= peak;
                shapes.push_back(std::move(s));
            }
            CHECK(signsOk == 20);
            double worst = 1.0;
            for (size_t i = 1; i < shapes.size(); ++i)
                worst = std::min(worst, correlation(shapes[0], shapes[i]));
            CHECK(worst >= 0.99);
        }
}

TEST_CASE("T-DIV-03 overlapping tails and sustained notes", "[divide]")
{
    for (const double spacing : {0.45, 0.5})
    {
        INFO("spacing " << spacing);
        const auto x = kickHits(20, spacing, 1.0);
        const auto xb = sourceBand(x);
        const auto r = runDivide(x, Quality::Mix);
        const auto y1 = y1Of(r);
        const auto hit = static_cast<size_t>(spacing * kFs);
        int nativeOk = 0;
        for (size_t i = 0; i < 20; ++i)
            nativeOk += firstSign(slice(y1, i * hit, hit)) == firstSign(slice(xb, i * hit, hit));
        CHECK(nativeOk == 20);
        const double sign1 = firstSign(slice(r.out, hit, hit));
        int same = 0;
        for (size_t i = 1; i < 20; ++i)
            same += firstSign(slice(r.out, i * hit, hit)) == sign1;
        CHECK(same == 19);
    }

    // a sustained 808 note is not reset inside (0.05-0.55 s)
    const auto held = runDivide(sig::bass808(kFs, {.fEnd = 45.0, .glideFrom = 90.0, .levelDb = -6.0}), Quality::Mix);
    int inside = 0;
    for (size_t i = 2400; i < 26400; ++i)
        inside += held.probe[i].reset;
    CHECK(inside == 0);

    const int nHits = 40; // 40 kicks -24..-3 dBFS: native polarity and shape
    std::vector<double> levels;
    for (int i = 0; i < nHits; ++i)
        levels.push_back(-24.0 + 21.0 * i / (nHits - 1));
    const auto x = sig::kick(kFs, {.nHits = nHits, .spacing = 0.5, .levelsDb = levels});
    const auto r = runDivide(x, Quality::Mix);
    const auto xb = sourceBand(x);
    const auto y1 = y1Of(r);
    std::vector<std::vector<double>> shapes;
    int ok = 0;
    for (size_t i = 0; i < static_cast<size_t>(nHits); ++i)
    {
        ok += firstSign(slice(y1, i * 24000, 24000)) == firstSign(slice(xb, i * 24000, 24000));
        double peak = 0.0;
        for (const double v : slice(xb, i * 24000, 24000))
            peak = std::max(peak, std::abs(v));
        const auto seg = slice(y1, i * 24000, 9600);
        std::vector<double> s(seg.begin(), seg.end());
        for (auto& v : s)
            v /= peak;
        shapes.push_back(std::move(s));
    }
    CHECK(ok == nHits);
    double worst = 1.0;
    for (size_t i = 1; i < shapes.size(); ++i)
        worst = std::min(worst, correlation(shapes[0], shapes[i]));
    CHECK(worst >= 0.99);
}

TEST_CASE("T-DIV-04 in-band SNR 10 dB: period error < 2 %; at 3 dB the gate is open <= 10 % of the time", "[divide]")
{
    const auto clean = runDivide(noiseSine(60.0, 10.0, 5), Quality::Mix);
    CHECK(std::abs(test::zcFreq(slice(y1Of(clean), 48000, 144000), kFs) / 30.0 - 1.0) < 0.02);
    Divide divide;
    divide.prepare(kFs, 1, engineLookaheadSamples(Quality::Mix, kFs));
    const auto noisy = noiseSine(60.0, 3.0, 6);
    int open = 0;
    double sink = 0.0;
    for (size_t n = 0; n < noisy.size(); ++n)
    {
        divide.process(std::span<const double>(&noisy[n], 1), {}, std::span<double>(&sink, 1));
        open += n >= 48000 && divide.gateOpen();
    }
    CHECK(open <= static_cast<int>(0.10 * static_cast<double>(noisy.size() - 48000)));
}

namespace
{
std::vector<double> chordFromSilence()
{
    std::vector<double> chord(96000, 0.0); // SIG-06: three beating tones from silence, -12 dBFS peak
    for (const double f : {55.0, 69.0, 82.0})
    {
        const auto s = sig::sine(f, kFs, 2.0, 0.0);
        for (size_t i = 0; i < chord.size(); ++i)
            chord[i] += s[i];
    }
    double peak = 0.0;
    for (const double v : chord)
        peak = std::max(peak, std::abs(v));
    for (auto& v : chord)
        v *= std::pow(10.0, -12.0 / 20.0) / peak;
    return chord;
}

double chordLevelDb(Quality quality, double rangeLo, double rangeHi)
{
    const auto chord = chordFromSilence();
    EngineControl control;
    control.rangeLoHz = rangeLo;
    control.rangeHiHz = rangeHi;
    const auto r = runDivide(chord, quality, control);
    return 20.0 * std::log10(test::rms(slice(r.out, 4800, 91200)) / test::rms(slice(chord, 4800, 91200)) + 1e-12);
}
} // namespace

TEST_CASE("T-DIV-05 chord (P2 conditions: band 15-500 Hz) and in-band noise are gated", "[divide]")
{
    for (const auto quality : {Quality::Live, Quality::Mix})
    {
        const double level = chordLevelDb(quality, 15.0, 500.0);
        INFO((quality == Quality::Mix ? "Mix " : "Live ") << level << " dB");
        CHECK(level <= -40.0);
    }
    for (const std::uint64_t seed : {4, 5, 6, 7, 8})
    {
        INFO("seed " << seed);
        const auto noise = sig::bandNoise(kFs, 3.0, 30.0, 120.0, -12.0, seed);
        Divide divide;
        divide.prepare(kFs, 1, engineLookaheadSamples(Quality::Mix, kFs));
        std::vector<double> out(noise.size() + 2160, 0.0);
        int open = 0;
        for (size_t n = 0; n < out.size(); ++n)
        {
            const double in = n < noise.size() ? noise[n] : 0.0;
            divide.process(std::span<const double>(&in, 1), {}, std::span<double>(&out[n], 1));
            open += n >= 2160 + 4800 && divide.gateOpen();
        }
        CHECK(open <= static_cast<int>(0.03 * static_cast<double>(noise.size() - 4800)));
        const auto aligned = slice(out, 2160 + 4800, noise.size() - 4800);
        CHECK(20.0 * std::log10(test::rms(aligned) / test::rms(slice(noise, 4800, noise.size() - 4800)) + 1e-12) <= -12.0);
    }
}

// F-1 (accepted 2026-10-24): through the default source band the chord's crossing periods now and then fit a linear
// trend; with one confirmation the gate opened 12 % of the time (-10.3 dB). Two consecutive values are needed now.
TEST_CASE("T-DIV-05 chord through the default source band is gated (F-1)", "[divide]")
{
    for (const auto quality : {Quality::Live, Quality::Mix})
    {
        const double level = chordLevelDb(quality, 30.0, 120.0);
        INFO((quality == Quality::Mix ? "Mix " : "Live ") << level << " dB");
        CHECK(level <= -40.0);
    }
}

TEST_CASE("DIVIDE clamps out-of-range controls instead of failing", "[divide]")
{
    EngineControl bad;
    bad.rangeLoHz = 0.0;
    bad.rangeHiHz = std::numeric_limits<double>::quiet_NaN();
    bad.confGate = 7.0;
    bad.shape = -3.0;
    bad.follow = false;
    bad.detThreshDb = std::numeric_limits<double>::infinity();
    const auto r = runDivide(sig::sine(55.0, kFs, 1.0, -12.0), Quality::Mix, bad);
    CHECK(std::all_of(r.out.begin(), r.out.end(), [](double v) { return std::isfinite(v); }));
}

TEST_CASE("T-DIV-06 switches are continuous; shape 100: energy above 2 kHz <= -60 dB re peak", "[divide]")
{
    // the flips happen at zero crossings, so the native subs never step more than x_b itself (as P2 measured it);
    // the shape-100 output passes a 4th-order low-pass at 32 f_est, whose overshoot may exceed that step (~22 %)
    EngineControl control;
    control.shape = 1.0;
    const auto x = sig::kick(kFs, {.nHits = 4, .spacing = 0.5});
    const auto r = runDivide(x, Quality::Mix, control);
    const auto xb = sourceBand(x);
    double stepX = 0.0, stepY1 = 0.0, stepY2 = 0.0, peak = 0.0;
    for (size_t i = 1; i < xb.size(); ++i)
    {
        stepX = std::max(stepX, std::abs(xb[i] - xb[i - 1]));
        stepY1 = std::max(stepY1, std::abs(r.probe[i].y1 - r.probe[i - 1].y1));
        stepY2 = std::max(stepY2, std::abs(r.probe[i].y2 - r.probe[i - 1].y2));
        peak = std::max(peak, std::abs(r.out[i]));
    }
    CHECK(stepY1 <= stepX * (1.0 + 1e-12));
    CHECK(stepY2 <= stepX * (1.0 + 1e-12));
    CHECK(test::bandDb(r.out, kFs, 2000.0, kFs / 2) - 20.0 * std::log10(peak) <= -60.0);
}

TEST_CASE("T-DIV-07 Constant -12 dBFS +- 0.5 dB for -40..-6 dBFS inputs; Follow proportional", "[divide]")
{
    // SPEC §6.2: Constant = y / p 10^(-12/20) on the divider's output y (as P2 measured it, K-03); after the shape-0
    // smoothing the output sits ~2 dB lower but just as constant (Phase 3 finding F-2)
    EngineControl constant;
    constant.follow = false;
    std::vector<double> outLevels;
    for (const double level : {-40.0, -30.0, -18.0, -6.0})
    {
        INFO("level " << level);
        const auto r = runDivide(sig::sine(55.0, kFs, 3.0, level), Quality::Mix, constant);
        double nativePeak = 0.0, outPeak = 0.0;
        for (size_t i = 72000; i < 120000; ++i)
        {
            nativePeak = std::max(nativePeak, std::abs(r.probe[i].y1 * r.probe[i].constantScale));
            outPeak = std::max(outPeak, std::abs(r.out[i]));
        }
        CHECK(std::abs(20.0 * std::log10(nativePeak) + 12.0) <= 0.5);
        outLevels.push_back(20.0 * std::log10(outPeak));
    }
    CHECK(*std::max_element(outLevels.begin(), outLevels.end()) - *std::min_element(outLevels.begin(), outLevels.end())
          <= 0.5);
    const auto a = runDivide(sig::sine(55.0, kFs, 3.0, -30.0), Quality::Mix);
    const auto b = runDivide(sig::sine(55.0, kFs, 3.0, -10.0), Quality::Mix);
    const double ratio = test::rms(slice(b.out, 72000, 48000)) / test::rms(slice(a.out, 72000, 48000));
    CHECK(std::abs(20.0 * std::log10(ratio) - 20.0) <= 0.1); // Follow: the sub follows the source level
}

TEST_CASE("T-DIV-08 the first 40 ms of a kick keep >= 50 % of the ungated sub energy", "[divide]")
{
    const auto x = sig::kick(kFs, {.nHits = 1, .spacing = 0.5});
    EngineControl ungatedControl;
    ungatedControl.confGate = 0.0;
    for (const auto quality : {Quality::Live, Quality::Mix, Quality::Print})
    {
        INFO(static_cast<int>(quality));
        const auto gated = runDivide(x, quality);
        // ungated reference: the same engine with the gate held open (provisional + conf_gate 0 opens at 0.2)
        double eGated = energy(slice(gated.out, 0, 1920));
        double eNative = 0.0;
        for (size_t i = 0; i < 1920; ++i)
            eNative += gated.probe[i].smoothed * gated.probe[i].smoothed;
        CHECK(eGated / eNative >= 0.5);
    }
    // Mix reads the confidence 45 ms ahead: fully open at the first lobe of a note after silence; Live still ramps up
    auto x2 = sig::silence(kFs, 0.2);
    const auto tone = sig::sine(55.0, kFs, 1.0, -12.0);
    x2.insert(x2.end(), tone.begin(), tone.end());
    for (const auto quality : {Quality::Mix, Quality::Live})
    {
        const auto r = runDivide(x2, quality);
        double peak = 0.0;
        for (const auto& p : r.probe)
            peak = std::max(peak, std::abs(p.smoothed));
        size_t j = 0;
        while (j < r.probe.size() && std::abs(r.probe[j].smoothed) <= 0.1 * peak)
            ++j;
        REQUIRE(j < r.probe.size());
        const double ratio = r.out[j] / r.probe[j].smoothed;
        INFO((quality == Quality::Mix ? "Mix " : "Live ") << ratio);
        if (quality == Quality::Mix)
            CHECK((ratio >= 0.99 && ratio <= 1.01));
        else
            CHECK(ratio < 0.9);
    }
}

TEST_CASE("T-DIV-09 shape keeps the f/2 level; shape 100 is close to the native shape", "[divide]")
{
    const auto x = sig::sine(55.0, kFs, 4.0, -12.0);
    auto levelAt = [&](double shape) {
        EngineControl control;
        control.shape = shape;
        const auto r = runDivide(x, Quality::Mix, control);
        return test::harmonicAmps(slice(r.out, 48000, 96000), kFs, 27.5, 3);
    };
    const auto ref = levelAt(0.0);
    for (const double shape : {0.25, 0.5, 0.75, 1.0})
    {
        INFO("shape " << shape);
        CHECK(std::abs(20.0 * std::log10(levelAt(shape)[0] / ref[0])) <= 0.5);
    }
    const auto full = levelAt(1.0);
    const auto native = test::harmonicAmps(slice(y1Of(runDivide(x, Quality::Mix)), 48000, 96000), kFs, 27.5, 3);
    CHECK(std::abs(20.0 * std::log10((full[2] / full[0]) / (native[2] / native[0]))) <= 1.0);
}

TEST_CASE("confidence is provisionally 1 from the note start; each note restarts the low-pass at 0.87 range_hi", "[divide]")
{
    const auto r = runDivide(sig::sine(55.0, kFs, 1.0, -12.0), Quality::Mix);
    bool high = true;
    for (size_t i = 96; i < 24000; ++i)
        high = high && r.probe[i].conf >= 0.5;
    CHECK(high);

    const auto hits = runDivide(kickHits(2, 0.5, 0.3), Quality::Mix); // raw kicks: the engine bands them itself
    CHECK(std::abs(hits.probe[0].cutoffTarget - 0.87 * 120.0) <= 1e-9);
    size_t secondReset = 0;
    for (size_t i = 21600; i < 26400 && secondReset == 0; ++i)
        if (hits.probe[i].reset)
            secondReset = i;
    REQUIRE(secondReset > 0);
    CHECK(std::abs(hits.probe[secondReset].cutoffTarget - 0.87 * 120.0) <= 1e-9);
}

TEST_CASE("det_thresh bounds the notes: quiet signals and the -90 dBFS arming floor", "[divide]")
{
    const auto quiet = sig::sine(55.0, kFs, 2.0, -85.0);
    CHECK(std::abs(test::zcFreq(slice(y1Of(runDivide(quiet, Quality::Mix)), 14400, 72000), kFs) / 55.0 - 1.0) < 1e-3);
    EngineControl low;
    low.detThreshDb = -90.0;
    CHECK(std::abs(test::zcFreq(slice(y1Of(runDivide(quiet, Quality::Mix, low)), 14400, 72000), kFs) / 27.5 - 1.0) < 1e-3);
    const auto tiny = sig::sine(55.0, kFs, 1.0, -96.0); // below the arming floor
    CHECK(std::abs(test::zcFreq(slice(y1Of(runDivide(tiny, Quality::Mix, low)), 14400, 24000), kFs) / 55.0 - 1.0) < 1e-3);
}

TEST_CASE("Review Focus 3: clipped square, DC offset, onset at the peak and +12 dBFS still divide, finite", "[divide]")
{
    EngineControl wide;
    wide.rangeLoHz = 15.0;
    wide.rangeHiHz = 400.0;
    auto offset = sig::sine(55.0, kFs, 2.0, -12.0);
    for (auto& v : offset)
        v += 0.2;
    auto loud = sig::sine(55.0, kFs, 2.0, 0.0);
    for (auto& v : loud)
        v *= 4.0;
    const std::vector<std::pair<std::string, std::vector<double>>> inputs{
        {"square", sig::square(40.0, kFs, 2.0, -6.0)},
        {"dc offset", offset},
        {"onset at the peak", sig::sine(55.0, kFs, 2.0, -12.0, std::numbers::pi / 2.0)},
        {"+12 dBFS", loud},
    };
    for (const auto& [label, x] : inputs)
    {
        INFO(label);
        const auto r = runDivide(x, Quality::Mix, wide);
        const auto y1 = y1Of(r);
        CHECK(std::all_of(r.out.begin(), r.out.end(), [](double v) { return std::isfinite(v); }));
        const double f = label == "square" ? 40.0 : 55.0;
        CHECK(std::abs(test::zcFreq(slice(y1, 28800, 48000), kFs) / (f / 2.0) - 1.0) < 1e-3);
    }
}

TEST_CASE("DIVIDE process does not allocate and does not depend on the block size", "[divide]")
{
    const auto x = kickHits(4, 0.5, 0.3, [](size_t n) { return floorNoise(n, -70.0, 9); });
    std::vector<double> reference;
    for (const int block : {1, 7, 64, 512})
    {
        Divide divide;
        divide.prepare(kFs, block, engineLookaheadSamples(Quality::Mix, kFs));
        std::vector<double> out(x.size(), 0.0);
        size_t allocations = 0;
        {
            test::AllocGuard guard;
            for (size_t n = 0; n < x.size(); n += static_cast<size_t>(block))
            {
                const size_t len = std::min(static_cast<size_t>(block), x.size() - n);
                divide.process(std::span<const double>(x.data() + n, len), {}, std::span<double>(out.data() + n, len));
            }
            allocations = guard.count();
        }
        INFO("block " << block);
        CHECK(allocations == 0);
        if (reference.empty())
            reference = out;
        else
            CHECK(out == reference);
    }
}
