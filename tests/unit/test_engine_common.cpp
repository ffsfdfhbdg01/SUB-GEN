// SPDX-License-Identifier: AGPL-3.0-or-later
// Common engine parts: quality and lookahead, source band (S3), activity (S4), note boundaries (SPEC §5.4),
// confidence gate (SPEC §6.1) and the envelope shaper.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

#include "core/Quality.h"
#include "dsp/Envelope.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "engines/Activity.h"
#include "engines/ConfidenceGate.h"
#include "engines/Engine.h"
#include "engines/EnvelopeShaper.h"
#include "engines/NoteTracker.h"
#include "engines/SourceBand.h"
#include "support/Golden.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::engines;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

std::vector<double> sourceBand(std::vector<double> x)
{
    SourceBand band;
    band.prepare(kFs);
    band.setRange(30.0, 120.0, 24);
    for (auto& v : x)
        v = band.process(v);
    return x;
}

// 30-120 Hz band noise at the given RMS level (the prototype's _floor, from xorshift instead of numpy).
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
    return sourceBand(std::move(x));
}

struct Notes
{
    std::vector<double> detect, begin, ends;
};

Notes runNotes(const std::vector<double>& xb, double threshDb = -55.0)
{
    Activity activity;
    activity.prepare(kFs);
    activity.setThreshold(threshDb);
    NoteTracker notes;
    notes.prepare(kFs);
    notes.setThreshold(threshDb);
    Notes out;
    for (size_t i = 0; i < xb.size(); ++i)
    {
        activity.process(xb[i]);
        const auto ev = notes.process(xb[i], activity.active());
        if (ev.start)
        {
            out.detect.push_back(ev.detect);
            out.begin.push_back(ev.begin);
        }
        if (ev.end)
            out.ends.push_back(static_cast<double>(i));
    }
    return out;
}

void checkOneStartPerHit(const Notes& n, double spacing, double tolMs, const char* label)
{
    INFO(label);
    REQUIRE(n.detect.size() == 20);
    for (size_t i = 0; i < 20; ++i)
    {
        const double t0 = static_cast<double>(i) * spacing * kFs;
        INFO("hit " << i);
        CHECK(n.begin[i] >= t0 - 1.0);
        CHECK(n.begin[i] <= t0 + tolMs * 1e-3 * kFs);
        CHECK(n.detect[i] - n.begin[i] <= 0.010 * kFs);
    }
}

std::vector<bool> gateSequence(double confGate, const std::vector<double>& conf, const std::vector<bool>& prov = {})
{
    ConfidenceGate gate;
    gate.prepare(kFs);
    gate.setGate(confGate);
    std::vector<bool> open;
    for (size_t i = 0; i < conf.size(); ++i)
    {
        gate.process(conf[i], !prov.empty() && prov[i]);
        open.push_back(gate.open());
    }
    return open;
}
} // namespace

TEST_CASE("lookahead at 48 kHz: Mix 2160, Print 2640 samples", "[engines-common]")
{
    CHECK(engineLookaheadSamples(Quality::Live, 48000.0) == 0);
    CHECK(engineLookaheadSamples(Quality::Mix, 48000.0) == 2160);
    CHECK(engineLookaheadSamples(Quality::Print, 48000.0) == 2640);
    CHECK(compLookaheadSamples(Quality::Mix, 48000.0) == 0);
    CHECK(compLookaheadSamples(Quality::Print, 48000.0) == 240);
    CHECK(limiterLookaheadSamples(Quality::Live, 48000.0) == 0);
    CHECK(limiterLookaheadSamples(Quality::Mix, 48000.0) == 96);
    CHECK(limiterLookaheadSamples(Quality::Print, 48000.0) == 240);
    // round((ms / 1000) fs) with ties to even, as the prototype's int(round(0.045 * fs)): 1984.5 -> 1984, 2425.5 -> 2426
    CHECK(engineLookaheadSamples(Quality::Mix, 44100.0) == 1984);
    CHECK(engineLookaheadSamples(Quality::Print, 44100.0) == 2426);
}

TEST_CASE("SourceBand equals the FilterCascade designs within 1e-12", "[engines-common]")
{
    using namespace subgen::dsp;
    const auto x = sig::xorshiftNoise(9600, 3);
    for (const int slope : {12, 24, 48})
    {
        SourceBand band;
        band.prepare(kFs);
        band.setRange(25.0, 140.0, slope);
        FilterCascade hp, lp;
        hp.set(designFilter(FilterFamily::Butterworth, FilterKind::HighPass, slopeToOrder(slope), 25.0, kFs));
        lp.set(designFilter(FilterFamily::Butterworth, FilterKind::LowPass, slopeToOrder(slope), 140.0, kFs));
        double worst = 0.0;
        for (const double v : x)
            worst = std::max(worst, std::abs(band.process(v) - lp.process(hp.process(v))));
        CHECK(worst <= 1e-12);
        const double gd = (hp.groupDelaySeconds(60.0, kFs) + lp.groupDelaySeconds(60.0, kFs)) * 1000.0;
        CHECK(std::abs(band.groupDelayMs(60.0) - gd) <= 1e-9);
    }
    // Fixed: +-1/2 octave around the centre, 24 dB/oct
    SourceBand fixed;
    fixed.prepare(kFs);
    fixed.setBandpass(60.0);
    FilterCascade hp, lp;
    hp.set(designFilter(FilterFamily::Butterworth, FilterKind::HighPass, 4, 60.0 / std::sqrt(2.0), kFs));
    lp.set(designFilter(FilterFamily::Butterworth, FilterKind::LowPass, 4, 60.0 * std::sqrt(2.0), kFs));
    double worst = 0.0;
    for (const double v : x)
        worst = std::max(worst, std::abs(fixed.process(v) - lp.process(hp.process(v))));
    CHECK(worst <= 1e-12);
}

TEST_CASE("Activity opens at the threshold and closes 3 dB lower after a 20 ms hold", "[engines-common]")
{
    // RMS -40 dBFS (on), then -57 dBFS (inside the 3 dB hysteresis: stays on), then -65 dBFS (off after the hold)
    std::vector<double> x;
    for (const double rmsDb : {-40.0, -57.0, -65.0})
    {
        const auto seg = sig::sine(55.0, kFs, 0.5, rmsDb + 10.0 * std::log10(2.0));
        x.insert(x.end(), seg.begin(), seg.end());
    }
    Activity activity;
    activity.prepare(kFs);
    activity.setThreshold(-55.0);
    dsp::RmsFollower rms; // the same 20 ms follower, to find where the level drops below -58 dBFS for good
    rms.prepare(kFs, 20.0);
    const double off = std::pow(10.0, -58.0 / 20.0);
    std::vector<bool> active(x.size());
    std::vector<double> gain(x.size());
    long firstBelow = -1;
    for (size_t i = 0; i < x.size(); ++i)
    {
        gain[i] = activity.process(x[i]);
        active[i] = activity.active();
        const double e = rms.process(x[i]);
        if (e >= off)
            firstBelow = -1;
        else if (firstBelow < 0)
            firstBelow = static_cast<long>(i);
    }
    CHECK(std::all_of(active.begin() + 2400, active.begin() + 48000, [](bool a) { return a; }));
    REQUIRE(firstBelow > 0);
    const auto offAt = static_cast<size_t>(firstBelow + 960); // hold: 20 ms
    CHECK(active[offAt - 1]);
    CHECK(!active[offAt]);
    CHECK(std::none_of(active.begin() + static_cast<long>(offAt), active.end(), [](bool a) { return a; }));
    CHECK(gain[offAt - 1] == 1.0);
    CHECK(std::abs(gain[offAt + 959] - std::exp(-1.0)) <= 0.01); // the mute gain releases over 20 ms
}

TEST_CASE("NoteTracker: one start per SIG-03 hit from silence, over band noise and hum", "[engines-common]")
{
    const auto hum = [](size_t n) {
        std::vector<double> h(n);
        for (size_t i = 0; i < n; ++i)
            h[i] = std::pow(10.0, -70.0 / 20.0) * std::sin(2.0 * std::numbers::pi * 31.0 * static_cast<double>(i) / kFs);
        return h;
    };
    checkOneStartPerHit(runNotes(kickHits(20, 0.5, 0.3)), 0.5, 3.0, "silence");
    checkOneStartPerHit(runNotes(kickHits(20, 0.5, 0.3, [](size_t n) { return floorNoise(n, -90.0, 5); })), 0.5, 3.0,
                        "noise -90");
    checkOneStartPerHit(runNotes(kickHits(20, 0.5, 0.3, [](size_t n) { return floorNoise(n, -60.0, 5); })), 0.5, 3.0,
                        "noise -60");
    checkOneStartPerHit(runNotes(kickHits(20, 0.5, 0.3, hum)), 0.5, 3.0, "hum -70");
}

TEST_CASE("NoteTracker: hits every 0.45 s over T60 1 s tails start 20 notes", "[engines-common]")
{
    // over a tail 13.5 dB down, the band-limited envelope of a new hit needs ~3.4 ms to rise above it
    checkOneStartPerHit(runNotes(kickHits(20, 0.45, 1.0)), 0.45, 5.0, "tails");
}

TEST_CASE("NoteTracker: sustained notes, noise, digital silence and note ends", "[engines-common]")
{
    const auto note = sourceBand(sig::bass808(kFs, {.fEnd = 45.0, .glideFrom = 90.0, .levelDb = -6.0}));
    CHECK(runNotes(note).detect.size() == 1);

    for (const std::uint64_t seed : {4, 5, 6, 7, 8})
    {
        INFO("seed " << seed);
        CHECK(runNotes(sig::bandNoise(kFs, 3.0, 30.0, 120.0, -12.0, seed)).detect.size() <= 3);
    }

    auto silenceThenSine = sig::silence(kFs, 0.2);
    const auto tone = sig::sine(55.0, kFs, 0.5, -12.0);
    silenceThenSine.insert(silenceThenSine.end(), tone.begin(), tone.end());
    const auto n = runNotes(silenceThenSine);
    REQUIRE(n.detect.size() == 1);
    CHECK(std::abs(n.begin[0] - 0.2 * kFs) <= 1.0);

    const auto ends = runNotes(kickHits(20, 0.5, 0.3)).ends;
    REQUIRE(ends.size() == 20);
    for (size_t i = 0; i < 20; ++i)
    {
        CHECK(ends[i] > static_cast<double>(i) * 0.5 * kFs);
        CHECK(ends[i] < static_cast<double>(i + 1) * 0.5 * kFs);
    }
}

TEST_CASE("NoteTracker golden: detect within 1e-6 samples, begin and ends exact", "[engines-common]")
{
    const auto golden = test::Golden::load("notes.sgg");
    const auto hits = sig::kick(kFs, {.nHits = 5, .spacing = 0.5, .levelDb = -6.0});
    auto noise = sig::bandNoise(kFs, 2.5, 30.0, 120.0, 0.0, 5);
    const double scale = std::pow(10.0, -60.0 / 20.0) / test::rms(noise);
    std::vector<double> noisy(hits.size());
    for (size_t i = 0; i < hits.size(); ++i)
        noisy[i] = hits[i] + noise[i] * scale;
    const auto tails = sig::kick(kFs, {.nHits = 5, .spacing = 0.45, .levelDb = -6.0, .t60 = 1.0});
    for (const auto& [name, x] : {std::pair{"silence", hits}, std::pair{"noise_60", noisy}, std::pair{"tails", tails}})
    {
        INFO(name);
        const auto n = runNotes(sourceBand(x));
        const auto detect = golden[std::string("detect/") + name];
        const auto begin = golden[std::string("begin/") + name];
        const auto ends = golden[std::string("end/") + name];
        REQUIRE(n.detect.size() == detect.size());
        REQUIRE(n.ends.size() == ends.size());
        for (size_t i = 0; i < detect.size(); ++i)
        {
            CHECK(std::abs(n.detect[i] - detect[i]) <= 1e-6);
            CHECK(n.begin[i] == begin[i]);
        }
        for (size_t i = 0; i < ends.size(); ++i)
            CHECK(n.ends[i] == ends[i]);
    }
}

TEST_CASE("ConfidenceGate reproduces the gate_mask sequences for conf_gate 0, 50 and 100", "[engines-common]")
{
    // hysteresis: opens at 0.5 (conf_gate 50), closes only below 0.3
    CHECK(gateSequence(0.5, {0.0, 0.2, 0.55, 0.45, 0.55, 0.40, 0.29, 0.45, 0.49, 0.5, 0.45})
          == std::vector<bool>{false, false, true, true, true, true, false, false, false, true, true});
    // D8 I1: closing threshold max(hi - 0.2, hi / 2): conf_gate 0 opens at 0.2 and closes below 0.1
    CHECK(gateSequence(0.0, {0.0, 0.9, 0.9, 0.05, 0.0, 0.0})
          == std::vector<bool>{false, true, true, false, false, false});
    CHECK(gateSequence(0.0, {0.0, 0.15, 0.2, 0.12, 0.09}) == std::vector<bool>{false, false, true, true, false});
    CHECK(gateSequence(1.0, {0.79, 0.8, 0.61, 0.59}) == std::vector<bool>{false, true, true, false});
}

TEST_CASE("ConfidenceGate: a provisional opening must be confirmed; a rejected one releases with 10 ms", "[engines-common]")
{
    CHECK(gateSequence(0.5, {1.0, 1.0, 0.4, 0.4, 0.6, 0.35, 0.29}, {true, true, false, false, false, false, false})
          == std::vector<bool>{true, true, false, false, true, true, false});
    CHECK(gateSequence(0.5, {1.0, 0.6, 0.35}, {true, false, false}) == std::vector<bool>{true, true, true});

    ConfidenceGate gate;
    gate.prepare(kFs);
    gate.setGate(0.5);
    double g0 = 0.0;
    for (int i = 0; i < 9600; ++i)
        g0 = gate.process(1.0, true); // provisional: open
    double g = 0.0;
    for (int i = 0; i < 480; ++i)
        g = gate.process(0.4, false); // the first real value is below 0.5: rejected
    CHECK(std::abs(g / g0 - std::exp(-1.0)) <= 0.02);
}

TEST_CASE("ConfidenceGate with two confirmations (DIVIDE, F-1) opens only on the second value", "[engines-common]")
{
    // divide.gate_mask(conf, 50, None, 2, count) with count = divide.confirmations(conf, None, 50)
    const std::vector<double> conf{0.0, 0.6, 0.6, 0.65, 0.4, 0.6, 0.29, 0.7, 0.75};
    const std::vector<int> count{0, 1, 1, 2, 0, 1, 0, 1, 2};
    ConfidenceGate gate;
    gate.prepare(kFs);
    gate.setGate(0.5);
    gate.setConfirmations(2);
    std::vector<bool> open;
    for (size_t i = 0; i < conf.size(); ++i)
    {
        gate.process(conf[i], false, count[i]);
        open.push_back(gate.open());
    }
    CHECK(open == std::vector<bool>{false, false, false, true, true, true, false, false, true});

    // a provisional opening is still confirmed by the first real value alone
    ConfidenceGate g2;
    g2.prepare(kFs);
    g2.setGate(0.5);
    g2.setConfirmations(2);
    std::vector<bool> open2;
    const std::vector<double> c2{1.0, 1.0, 0.6, 0.29};
    const std::vector<bool> p2{true, true, false, false};
    const std::vector<int> n2{1, 1, 2, 0};
    for (size_t i = 0; i < c2.size(); ++i)
    {
        g2.process(c2[i], p2[i], n2[i]);
        open2.push_back(g2.open());
    }
    CHECK(open2 == std::vector<bool>{true, true, true, false});
}

TEST_CASE("ConfidenceGate gain: attack 5 ms, release 50 ms", "[engines-common]")
{
    ConfidenceGate gate;
    gate.prepare(kFs);
    gate.setGate(0.5);
    double g = 0.0;
    for (int i = 0; i < 240; ++i)
        g = gate.process(0.55, false);
    CHECK(std::abs(g - (1.0 - std::exp(-1.0))) <= 0.02);
    for (int i = 240; i < 24000; ++i)
        gate.process(0.55, false);
    for (int i = 0; i < 2400; ++i)
        g = gate.process(0.0, false);
    CHECK(std::abs(g - std::exp(-1.0)) <= 0.02); // one release time constant
}

TEST_CASE("EnvelopeShaper with dynamics 0 is the plain follower", "[engines-common]")
{
    const auto levels = sig::xorshiftNoise(20000, 29);
    const auto onsets = sig::xorshiftNoise(20000, 31);
    EnvelopeShaper shaper;
    shaper.prepare(kFs);
    shaper.set(true, 3.0, 80.0, 0.0);
    dsp::PeakFollower plain;
    plain.prepare(kFs, 3.0, 80.0);
    bool same = true;
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const double level = std::abs(levels[i]);
        same = same && shaper.process(level, level + 0.3, level, onsets[i] > 0.99) == plain.process(level);
    }
    CHECK(same);

    shaper.set(false, 3.0, 80.0, 0.0); // Constant: -12 dBFS peak (gated by the activity elsewhere)
    CHECK(shaper.process(0.9, 1.0, 0.9, false) == std::pow(10.0, -12.0 / 20.0));
}

TEST_CASE("EnvelopeShaper dynamics: +1 quadruples release and attack, -1 shortens release and adds a 5 ms accent", "[engines-common]")
{
    auto timeConstantSamples = [](double dynamics, double from, double to) {
        EnvelopeShaper s;
        s.prepare(kFs);
        s.set(true, 2.0, 50.0, dynamics);
        for (int i = 0; i < 96000; ++i)
            s.process(from, from, from, false);
        const double target = to + (from - to) * std::exp(-1.0);
        for (int i = 0; i < 96000; ++i)
            if (const double v = s.process(to, to, to, false); from > to ? v <= target : v >= target)
                return i + 1;
        return -1;
    };
    CHECK(std::abs(timeConstantSamples(1.0, 1.0, 0.0) - 4 * 2400) <= 2);  // release 50 ms x 4^1
    CHECK(std::abs(timeConstantSamples(-1.0, 1.0, 0.0) - 2400 / 4) <= 2); // release 50 ms x 4^-1
    CHECK(std::abs(timeConstantSamples(1.0, 0.0, 1.0) - 4 * 96) <= 2);   // attack 2 ms x (1 + 3)
    CHECK(std::abs(timeConstantSamples(0.0, 0.0, 1.0) - 96) <= 2);

    EnvelopeShaper s;
    s.prepare(kFs);
    s.set(true, 2.0, 50.0, -1.0);
    dsp::PeakFollower plain;
    plain.prepare(kFs, 2.0, 50.0 / 4.0);
    for (int i = 0; i < 480; ++i)
    {
        const double v = s.process(0.2, 0.7, 0.2, i == 0);
        const double expected = plain.process(0.2) + (i < 240 ? 0.5 : 0.0); // (-d)(e_pk - e_rms) for 5 ms
        CHECK(std::abs(v - expected) <= 1e-12);
    }
}
