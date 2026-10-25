// SPDX-License-Identifier: AGPL-3.0-or-later
// TRIGGER (SPEC §6.4, T-TRIG-01..04): the onset detector, the closed-form voice, Restart and Legato, velocity and
// Follow, against the prototype (tests/golden/trigger.sgg, proto/subgen_proto/trigger.py) and the TEST_PLAN criteria;
// and TRACK's Reset phase mode, which uses the same detector.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "core/Quality.h"
#include "dsp/Envelope.h"
#include "engines/OnsetDetector.h"
#include "engines/SourceBand.h"
#include "engines/Track.h"
#include "engines/Trigger.h"
#include "support/Golden.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::engines;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The prototype's voice parameters: Fixed base_freq 45 Hz is f_end.
EngineControl prototypeDefaults()
{
    EngineControl c;
    c.baseFixed = true;
    c.baseFreqHz = 45.0;
    return c;
}

struct Run
{
    std::vector<double> out;          // aligned with the input (the lookahead removed)
    std::vector<TriggerProbe> probe;  // aligned with the input
    std::vector<double> onsets;       // voice onsets (input samples, fractional)
};

Run runTrigger(const std::vector<double>& x, Quality quality, const EngineControl& control, double fs = kFs,
               const std::vector<double>* key = nullptr)
{
    Trigger trigger;
    const int look = engineLookaheadSamples(quality, fs);
    trigger.prepare(fs, 1, look);
    trigger.setControl(control);
    const size_t total = x.size() + static_cast<size_t>(look);
    std::vector<double> rawOut(total);
    std::vector<TriggerProbe> rawProbe(total);
    for (size_t n = 0; n < total; ++n)
    {
        const double in = n < x.size() ? x[n] : 0.0;
        const double k = key != nullptr && n < key->size() ? (*key)[n] : 0.0;
        trigger.process(std::span<const double>(&in, 1), std::span<const double>(&k, 1),
                        std::span<double>(&rawOut[n], 1));
        rawProbe[n] = trigger.probe();
    }
    Run r;
    r.out.assign(rawOut.begin() + look, rawOut.end());
    r.probe.assign(rawProbe.begin() + look, rawProbe.end());
    for (const auto& p : r.probe)
        if (p.onset)
            r.onsets.push_back(p.onsetTime);
    return r;
}

std::vector<double> renderBlocks(const std::vector<double>& x, Quality quality, const EngineControl& control,
                                 size_t block)
{
    Trigger trigger;
    const int look = engineLookaheadSamples(quality, kFs);
    trigger.prepare(kFs, static_cast<int>(block), look);
    trigger.setControl(control);
    std::vector<double> in(x);
    in.resize(x.size() + static_cast<size_t>(look), 0.0);
    std::vector<double> out(in.size());
    const std::vector<double> key(block, 0.0);
    for (size_t i = 0; i < in.size(); i += block)
    {
        const size_t len = std::min(block, in.size() - i);
        trigger.process(std::span<const double>(in.data() + i, len), std::span<const double>(key.data(), len),
                        std::span<double>(out.data() + i, len));
    }
    return {out.begin() + look, out.end()};
}

std::vector<double> sourceBand(const std::vector<double>& x)
{
    SourceBand band;
    band.prepare(kFs);
    band.setRange(30.0, 120.0, 24);
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = band.process(x[i]);
    return y;
}

std::vector<double> detect(const std::vector<double>& x, double threshDb = -30.0, double holdMs = 20.0,
                           double lockoutMs = 60.0)
{
    OnsetDetector det;
    det.prepare(kFs);
    det.set(threshDb, holdMs, lockoutMs);
    std::vector<double> onsets;
    for (size_t i = 0; i < x.size(); ++i)
        if (const auto offset = det.process(x[i]))
            onsets.push_back(static_cast<double>(i) + *offset);
    return onsets;
}

// 100 Hz sine bursts (zero start phase), one every `spacing` s.
std::vector<double> bursts(int count, double spacing, double amp = 0.05, double lengthMs = 10.0)
{
    std::vector<double> x(static_cast<size_t>(std::lround(count * spacing * kFs)), 0.0);
    const auto len = static_cast<size_t>(lengthMs * 1e-3 * kFs);
    for (int k = 0; k < count; ++k)
    {
        const auto start = static_cast<size_t>(std::lround(k * spacing * kFs));
        for (size_t i = 0; i < len && start + i < x.size(); ++i)
            x[start + i] = amp * std::sin(2.0 * std::numbers::pi * 100.0 * static_cast<double>(i) / kFs);
    }
    return x;
}

// The largest second difference |y[n] - 2 y[n-1] + y[n-2]| within +-1 ms of `centre` (a step of s shows as s).
double click(const std::vector<double>& y, double centre)
{
    const auto h = static_cast<std::ptrdiff_t>(0.001 * kFs);
    const auto c = static_cast<std::ptrdiff_t>(centre);
    double worst = 0.0;
    for (std::ptrdiff_t i = std::max<std::ptrdiff_t>(c - h + 2, 2); i <= c + h && i < static_cast<std::ptrdiff_t>(y.size()); ++i)
        worst = std::max(worst, std::abs(y[static_cast<size_t>(i)] - 2.0 * y[static_cast<size_t>(i - 1)] +
                                         y[static_cast<size_t>(i - 2)]));
    return worst;
}

// The prototype's phase advance 2 pi int_0^s f, f(t) = 45 + 35 exp(-t / 30 ms).
double phaseAdvance(double s) { return 2.0 * std::numbers::pi * (45.0 * s + 35.0 * 0.030 * (1.0 - std::exp(-s / 0.030))); }

double wrapPi(double x) { return x - 2.0 * std::numbers::pi * std::floor((x + std::numbers::pi) / (2.0 * std::numbers::pi)); }
} // namespace

TEST_CASE("onset detector: interpolated crossing, lockout, hold, silence", "[trigger]")
{
    {
        // |x| steps to 0.2 at n0: the follower gives e[n0 + j] = A (1 - r^(j+1)); the crossing interpolates linearly
        const size_t n0 = 4800;
        std::vector<double> x(24000, 0.0);
        std::fill(x.begin() + n0, x.end(), 0.2);
        const double r = std::exp(-1.0 / (0.2e-3 * kFs));
        const double theta = std::pow(10.0, -30.0 / 20.0);
        auto e = [&](int j) { return 0.2 * (1.0 - std::pow(r, j + 1)); };
        int j = 0;
        while (!(e(j) < theta && theta <= e(j + 1)))
            ++j;
        const auto onsets = detect(x);
        REQUIRE(onsets.size() == 1);
        CHECK(std::abs(onsets[0] - (static_cast<double>(n0) + j + (theta - e(j)) / (e(j + 1) - e(j)))) <= 1e-6);
    }
    {
        // a burst every 30 ms: hits closer than the lockout trigger once
        const auto x = bursts(8, 0.030);
        CHECK(detect(x, -30.0, 0.0, 20.0).size() == 8);
        const auto on50 = detect(x, -30.0, 0.0, 50.0);
        CHECK(on50.size() == 4);
        for (size_t i = 1; i < on50.size(); ++i)
            CHECK(on50[i] - on50[i - 1] >= 0.050 * kFs);
        const auto on60 = detect(x, -30.0, 0.0, 60.0);
        REQUIRE(on60.size() >= 2);
        CHECK(on60[1] - on60[0] > 0.045 * kFs);
    }
    {
        // re-arming needs the envelope 3 dB under the threshold for the hold time: 27 ms of dip suffice for 20 ms
        std::vector<double> x(static_cast<size_t>(0.02 * kFs), 0.2);
        x.resize(static_cast<size_t>(0.08 * kFs), 0.0);
        x.resize(static_cast<size_t>(0.10 * kFs), 0.2);
        CHECK(detect(x, -30.0, 20.0, 5.0).size() == 2);
        CHECK(detect(x, -30.0, 40.0, 5.0).size() == 1);
    }
    CHECK(detect(std::vector<double>(48000, 0.0)).empty());
    auto quiet = sig::bandNoise(kFs, 1.0, 20.0, 2000.0, -50.0, 3);
    CHECK(detect(quiet).empty());
}

TEST_CASE("T-TRIG-01 SIG-03: onset error <= 1 sample (Mix/Print) and <= 2 ms (Live), no double triggers, constant bias "
          "-0.486 +- 0.05 sample",
          "[trigger]")
{
    // the detector on the kick, as P2-04: against the same follower on a 64 x finer grid
    const auto x = sig::kick(kFs, {.nHits = 4, .spacing = 0.5, .levelDb = -6.0});
    const auto onsets = detect(x);
    REQUIRE(onsets.size() == 4); // one per hit
    const double fine = 64.0 * kFs;
    const auto xf = sig::kick(fine, {.nHits = 1, .spacing = 0.02, .levelDb = -6.0});
    dsp::PeakFollower follower;
    follower.prepare(fine, 0.2, 15.0);
    const double theta = std::pow(10.0, -30.0 / 20.0);
    double reference = kNaN;
    double previous = 0.0;
    for (size_t i = 0; i < xf.size() && std::isnan(reference); ++i)
    {
        const double e = follower.process(xf[i]);
        if (i > 0 && previous < theta && e >= theta)
            reference = (static_cast<double>(i - 1) + (theta - previous) / (e - previous)) / 64.0;
        previous = e;
    }
    std::vector<double> error;
    for (size_t k = 0; k < onsets.size(); ++k)
    {
        error.push_back(onsets[k] - (24000.0 * static_cast<double>(k) + reference));
        CHECK(std::abs(error.back()) <= 1.0);
        const double latency = onsets[k] - 24000.0 * static_cast<double>(k); // Live: from the true hit start
        CHECK(latency >= 0.0);
        CHECK(latency <= 0.002 * kFs);
    }
    double mean = 0.0;
    for (const double e : error)
        mean += e / static_cast<double>(error.size());
    INFO("bias " << mean);
    CHECK(std::abs(mean + 0.486) <= 0.05);
    for (const double e : error)
        CHECK(std::abs(e - mean) <= 0.05);
    // the engine starts every voice at the detected time on x_b, in every quality, and nowhere else
    const auto xb = sourceBand(x);
    const auto expected = detect(xb);
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        const auto r = runTrigger(x, q, prototypeDefaults());
        REQUIRE(r.onsets.size() == expected.size());
        for (size_t k = 0; k < expected.size(); ++k)
        {
            CHECK(std::abs(r.onsets[k] - expected[k]) <= 1e-9);
            const auto first = static_cast<size_t>(std::ceil(expected[k]));
            CHECK(r.probe[first].onset);
            if (k == 0)
                CHECK(r.out[first - 1] == 0.0);
        }
    }
}

TEST_CASE("T-TRIG-02 hits 1..99 identical within -120 dB; block sizes {1, 7, 64, 512} give identical output", "[trigger]")
{
    const auto x = sig::kick(kFs, {.nHits = 100, .spacing = 0.5, .levelDb = -6.0});
    const auto c = prototypeDefaults();
    const auto y = renderBlocks(x, Quality::Mix, c, 1);
    const size_t hit = 24000;
    auto seg = [&](size_t i) { return std::span<const double>(y.data() + i * hit, hit); };
    double worst = -400.0;
    for (size_t i = 2; i < 100; ++i)
        worst = std::max(worst, test::nullDb(seg(1), seg(i)));
    INFO("worst difference " << worst << " dB");
    CHECK(worst <= -120.0);
    for (const size_t block : {7, 64, 512})
    {
        INFO("block " << block);
        CHECK(renderBlocks(x, Quality::Mix, c, block) == y);
    }
}

TEST_CASE("T-TRIG-03 time-domain f(t) within 0.5 % and A(t) within 0.1 dB of the formulas", "[trigger]")
{
    // velocity 0 (V = 1), Constant: the bare voice, 80 -> 45 Hz in 30 ms, attack 2 ms, T60 350 ms
    auto c = prototypeDefaults();
    c.trigVel = 0.0;
    c.follow = false;
    const auto r = runTrigger(sig::kick(kFs, {.nHits = 1, .spacing = 0.5, .levelDb = -6.0}), Quality::Mix, c);
    REQUIRE(r.onsets.size() == 1);
    const double t0 = r.onsets[0];
    const auto& y = r.out;
    std::vector<double> crossings;
    for (size_t i = 1; i < y.size(); ++i)
        if ((y[i - 1] < 0.0 && y[i] >= 0.0) || (y[i - 1] > 0.0 && y[i] <= 0.0))
            crossings.push_back(static_cast<double>(i - 1) + y[i - 1] / (y[i - 1] - y[i]));
    int checked = 0;
    double worstF = 0.0;
    for (size_t k = 1; k < crossings.size(); ++k)
    {
        const double t = (0.5 * (crossings[k] + crossings[k - 1]) - t0) / kFs;
        if (t < 0.003 || t > 0.4)
            continue;
        const double f = 0.5 * kFs / (crossings[k] - crossings[k - 1]);
        worstF = std::max(worstF, std::abs(f / (45.0 + 35.0 * std::exp(-t / 0.030)) - 1.0));
        ++checked;
    }
    CHECK(checked > 20);
    CHECK(worstF <= 0.005);
    int peaks = 0;
    double worstA = 0.0;
    for (size_t i = 1; i + 1 < y.size(); ++i)
    {
        const double t = (static_cast<double>(i) - t0) / kFs;
        if (t < 0.003 || t > 0.2 || !(std::abs(y[i]) > std::abs(y[i - 1]) && std::abs(y[i]) >= std::abs(y[i + 1])))
            continue;
        worstA = std::max(worstA, std::abs(20.0 * std::log10(std::abs(y[i]) / std::pow(10.0, -3.0 * (t - 0.002) / 0.350))));
        ++peaks;
    }
    CHECK(peaks > 10);
    CHECK(worstA <= 0.1);
}

TEST_CASE("T-TRIG-04 Restart click (max 2nd difference in +-1 ms) <= 1e-3; Legato carries phase and amplitude within "
          "1e-12; attack 0 at a 90 deg start phase: max step <= 0.06",
          "[trigger]")
{
    {
        // two kicks 100 ms apart; the first is still sounding, so -20 dBFS and no hold
        auto c = prototypeDefaults();
        c.trigThreshDb = -20.0;
        c.trigHoldMs = 0.0;
        const auto r = runTrigger(sig::kick(kFs, {.nHits = 2, .spacing = 0.1, .levelDb = -6.0}), Quality::Mix, c);
        REQUIRE(r.onsets.size() == 2);
        CHECK(click(r.out, r.onsets[1]) <= 1e-3);
    }
    {
        // Legato: y(s) = A(s) sin(phi_c + inc(s)), A rising linearly from the carried amplitude to 1 over the attack
        auto c = prototypeDefaults();
        c.trigLegato = true;
        c.trigVel = 0.0;
        c.follow = false;
        c.trigThreshDb = -12.0;
        c.trigHoldMs = 0.0;
        c.trigLockoutMs = 5.0;
        for (const double spacing : {0.04, 0.06, 0.10, 0.15, 0.20})
        {
            INFO("spacing " << spacing);
            const auto r = runTrigger(bursts(2, spacing, 0.9), Quality::Mix, c);
            REQUIRE(r.onsets.size() == 2);
            const double t0 = r.onsets[0];
            const double t1 = r.onsets[1];
            const double sOld = (t1 - t0) / kFs;
            const double phiC = phaseAdvance(sOld);
            const double aC = sOld < 0.002 ? sOld / 0.002 : std::pow(10.0, -3.0 * (sOld - 0.002) / 0.350);
            double worst = 0.0;
            for (auto k = static_cast<size_t>(std::ceil(t1)); k < r.out.size(); ++k)
            {
                const double s = (static_cast<double>(k) - t1) / kFs;
                const double a = s < 0.002 ? aC + (1.0 - aC) * s / 0.002 : std::pow(10.0, -3.0 * (s - 0.002) / 0.350);
                worst = std::max(worst, std::abs(r.out[k] - a * std::sin(phiC + phaseAdvance(s))));
            }
            CHECK(worst <= 1e-12);
        }
    }
    {
        auto c = prototypeDefaults();
        c.trigAttackMs = 0.0;
        c.trigPhaseDeg = 90.0;
        c.trigVel = 0.0;
        c.follow = false;
        const auto r = runTrigger(sig::kick(kFs, {.nHits = 1, .spacing = 0.2, .levelDb = -6.0}), Quality::Mix, c);
        double step = 0.0;
        for (size_t i = 1; i < r.out.size(); ++i)
            step = std::max(step, std::abs(r.out[i] - r.out[i - 1]));
        CHECK(step <= 0.06);
    }
}

TEST_CASE("Follow is causal: a prefix of the input gives the same prefix of the output (1e-12)", "[trigger]")
{
    // D8 M4: a louder second kick inside the lockout must not change what came before it; in Mix and Print the
    // output may look ahead by the lookahead only
    auto x = sig::kick(kFs, {.nHits = 1, .spacing = 0.5, .levelDb = -12.0});
    const auto louder = sig::kick(kFs, {.nHits = 1, .spacing = 0.5, .levelDb = -6.0});
    const auto at = static_cast<size_t>(0.030 * kFs);
    for (size_t i = at; i < x.size(); ++i)
        x[i] += louder[i - at];
    const auto k = static_cast<size_t>(0.025 * kFs); // outputs compared: the first 25 ms
    for (const Quality q : {Quality::Live, Quality::Mix})
    {
        // the prefix holds the input those outputs may see: up to the lookahead past them
        const size_t length = k + static_cast<size_t>(engineLookaheadSamples(q, kFs));
        const std::vector<double> prefix(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(length));
        const auto full = runTrigger(x, q, prototypeDefaults()).out;
        const auto part = runTrigger(prefix, q, prototypeDefaults()).out;
        double worst = 0.0;
        for (size_t i = 0; i < k; ++i)
            worst = std::max(worst, std::abs(full[i] - part[i]));
        CHECK(worst <= 1e-12);
    }
}

TEST_CASE("overlapping retriggers stay finite without clicks", "[trigger]")
{
    // short bursts every 30 ms (the envelope re-arms in the gaps), hold 0, lockout 5 ms: every retrigger lands on a
    // voice that is still sounding (T60 350 ms)
    auto c = prototypeDefaults();
    c.trigHoldMs = 0.0;
    c.trigLockoutMs = 5.0;
    const auto x = bursts(12, 0.03);
    for (const bool legato : {false, true})
    {
        INFO((legato ? "Legato" : "Restart"));
        c.trigLegato = legato;
        const auto r = runTrigger(x, Quality::Mix, c);
        CHECK(r.onsets.size() >= 6);
        bool finite = true;
        for (const double v : r.out)
            finite = finite && std::isfinite(v);
        CHECK(finite);
        double worst = 0.0;
        for (size_t i = 1; i < r.onsets.size(); ++i)
            worst = std::max(worst, click(r.out, r.onsets[i]));
        CHECK(worst <= (legato ? 0.01 : 1e-3)); // Legato keeps a slope kink (SPEC: <= 0.008)
    }
}

TEST_CASE("the key input triggers the voice when key_mode is Trigger", "[trigger]")
{
    auto c = prototypeDefaults();
    c.keyMode = KeyMode::Trigger;
    const auto key = sig::kick(kFs, {.nHits = 2, .spacing = 0.25, .levelDb = -6.0});
    const std::vector<double> silence(key.size(), 0.0);
    const auto r = runTrigger(silence, Quality::Mix, c, kFs, &key);
    const auto expected = detect(key); // the key itself, not band-limited
    REQUIRE(r.onsets.size() == expected.size());
    for (size_t k = 0; k < expected.size(); ++k)
        CHECK(std::abs(r.onsets[k] - expected[k]) <= 1e-9);
    c.keyMode = KeyMode::None;
    CHECK(runTrigger(silence, Quality::Mix, c, kFs, &key).onsets.empty());
}

TEST_CASE("Auto takes f_end = f0 / 2 of the tracked pitch at the trigger, within 20-100 Hz", "[trigger]")
{
    // an accent of a 110 Hz bass note (-24 -> -6 dBFS at 0.5 s) triggers without disturbing its pitch
    EngineControl c;
    c.trigThreshDb = -12.0;
    auto x = sig::sine(110.0, kFs, 1.0, 0.0);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] *= i < 24000 ? std::pow(10.0, -24.0 / 20.0) : std::pow(10.0, -6.0 / 20.0);
    for (const Quality q : {Quality::Live, Quality::Mix})
    {
        INFO((q == Quality::Live ? "Live" : "Mix"));
        const auto r = runTrigger(x, q, c);
        REQUIRE(r.onsets.size() == 1);
        const auto at = std::find_if(r.probe.begin(), r.probe.end(), [](const TriggerProbe& p) { return p.onset; });
        CHECK(std::abs(at->fEnd - 55.0) <= 1.0);
    }
    // before any pitch: base_freq
    c.baseFreqHz = 52.0;
    const auto r = runTrigger(sig::kick(kFs, {.nHits = 1, .spacing = 0.3, .levelDb = -6.0}), Quality::Live, c);
    const auto first = std::find_if(r.probe.begin(), r.probe.end(), [](const TriggerProbe& p) { return p.onset; });
    REQUIRE(first != r.probe.end());
    CHECK(first->fEnd == 52.0);
}

TEST_CASE("TRACK Reset: phase 0 +- 1 deg at the onset", "[trigger]")
{
    // the onset detector of §6.4 sets the oscillator phase to 0 at the fractional onset time
    EngineControl c;
    c.phaseGen = PhaseGen::Reset;
    auto x = sig::silence(kFs, 0.2);
    const auto note = sig::sine(55.0, kFs, 0.4, -6.0);
    x.insert(x.end(), note.begin(), note.end());
    x.resize(x.size() + static_cast<size_t>(0.2 * kFs), 0.0);
    x.insert(x.end(), note.begin(), note.end());
    Track track;
    const int look = engineLookaheadSamples(Quality::Mix, kFs);
    track.prepare(kFs, 1, look);
    track.setControl(c);
    std::vector<TrackProbe> probe(x.size() + static_cast<size_t>(look));
    std::vector<double> onsetAt(probe.size(), kNaN);
    const double k0 = 0.0;
    for (size_t n = 0; n < probe.size(); ++n)
    {
        const double in = n < x.size() ? x[n] : 0.0;
        double out = 0.0;
        track.process(std::span<const double>(&in, 1), std::span<const double>(&k0, 1), std::span<double>(&out, 1));
        probe[n] = track.probe();
        onsetAt[n] = track.lastOnsetTime();
    }
    int checked = 0;
    for (size_t n = static_cast<size_t>(look) + 1; n < probe.size(); ++n)
    {
        const double t = onsetAt[n];
        if (std::isnan(t) || t == onsetAt[n - 1])
            continue;
        // the first output sample at or after the onset: its phase is 2 pi f (m - t) / fs
        const double m = static_cast<double>(n - static_cast<size_t>(look));
        const auto& p = probe[n];
        REQUIRE(p.subFreq > 0.0);
        const double expected = 2.0 * std::numbers::pi * p.subFreq * (m - t) / kFs;
        CHECK(std::abs(wrapPi(p.phase1 - expected)) * 180.0 / std::numbers::pi <= 1.0);
        ++checked;
    }
    CHECK(checked == 2);
}

TEST_CASE("golden voice within 1e-9", "[trigger]")
{
    const auto golden = test::Golden::load("trigger.sgg");
    auto defaults = prototypeDefaults();
    auto sub2 = defaults;
    sub2.sub1Gain = 0.0;
    sub2.sub2Gain = 1.0;
    auto legato = defaults;
    legato.trigLegato = true;
    legato.trigThreshDb = -20.0;
    legato.trigHoldMs = 0.0;
    struct Case
    {
        const char* name;
        std::vector<double> input;
        EngineControl control;
    };
    const std::vector<Case> cases{
        {"kick3", sig::kick(kFs, {.nHits = 3, .spacing = 0.2, .levelDb = -6.0}), defaults},
        {"kick1_sub2", sig::kick(kFs, {.nHits = 1, .spacing = 0.2, .levelDb = -6.0}), sub2},
        {"legato_close", sig::kick(kFs, {.nHits = 3, .spacing = 0.1, .levelDb = -6.0}), legato},
    };
    for (const auto& c : cases)
    {
        INFO(c.name);
        const auto want = golden[std::string("out/") + c.name]; // 4096-sample windows from each hit's start
        const auto starts = golden[std::string("starts/") + c.name];
        const auto r = runTrigger(c.input, Quality::Mix, c.control);
        REQUIRE(want.size() == 4096 * starts.size());
        double worst = 0.0;
        for (size_t w = 0; w < starts.size(); ++w)
            for (size_t i = 0; i < 4096; ++i)
                worst = std::max(worst, std::abs(r.out[static_cast<size_t>(starts[w]) + i] - want[w * 4096 + i]));
        CHECK(worst <= 1e-9);
    }
}
