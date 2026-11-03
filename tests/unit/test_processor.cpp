// SPDX-License-Identifier: AGPL-3.0-or-later
// The plug-in processor (SPEC §4, §9; SPEC_IO_UI §7.4): the S1-S8 flow, summation and bypass, latency, invariance,
// real-time safety, stability and the warnings.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include "ProcessorHarness.h"
#include "chain/Drive.h"
#include "core/Quality.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "engines/EngineHost.h"
#include "engines/SourceBand.h"
#include "plugin/Latency.h"
#include "plugin/Warnings.h"
#include "chain/PhaseTool.h"
#include "params/Sanitise.h"
#include "support/AllocGuard.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::test;
using plugin::SubGenProcessor;
using plugin::Warning;
using Set = juce::AudioChannelSet;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

Channels same(const std::vector<double>& x, int channels) { return Channels(static_cast<size_t>(channels), x); }

float qualityValue(Quality q) { return static_cast<float>(static_cast<int>(q)); }

bool delayedExactly(const Channels& out, const Channels& in, size_t latency)
{
    for (size_t c = 0; c < out.size(); ++c)
        for (size_t i = 0; i < out[c].size(); ++i)
            if (out[c][i] != (i >= latency ? in[c][i - latency] : 0.0))
                return false;
    return true;
}

double nullDb(const std::vector<double>& a, const std::vector<double>& b)
{
    return test::nullDb(std::span<const double>(a), std::span<const double>(b));
}

double maxFirstDifference(const std::vector<double>& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, y.size()); ++i)
        worst = std::max(worst, std::abs(y[i] - y[i - 1]));
    return worst;
}

bool finite(const Channels& y)
{
    for (const auto& c : y)
        for (const double v : c)
            if (!std::isfinite(v))
                return false;
    return true;
}

size_t firstAbove(const std::vector<double>& y, double threshold)
{
    for (size_t i = 0; i < y.size(); ++i)
        if (std::abs(y[i]) > threshold)
            return i;
    return y.size();
}
} // namespace

TEST_CASE("T-SUM-01 sub Off: output equals the input delayed by L_total, bit-exact, float and double, every quality, "
          "layouts L1 and a sample of L2",
          "[processor]")
{
    struct Variant
    {
        Quality q;
        bool linear;
    };
    for (const auto& set : {Set::stereo(), Set::create5point1(), Set::create7point1point2(), Set::create7point1point4(),
                            Set::mono(), Set::createLCR(), Set::quadraphonic(), Set::create7point1(), Set::create9point1point6()})
        for (const auto& v : {Variant{Quality::Live, false}, Variant{Quality::Mix, false}, Variant{Quality::Print, false},
                              Variant{Quality::Print, true}}) // Print also with Linear Phase: L_lin in the dry's delay
            for (const bool useFloat : {false, true})
            {
                INFO(set.getDescription() << ", quality " << static_cast<int>(v.q) << (v.linear ? ", Linear Phase" : "")
                                          << (useFloat ? ", float" : ", double"));
                auto p = makeProcessor({set, set}, kFs, 512, [&](SubGenProcessor& s) {
                    setParam(s, "quality", qualityValue(v.q));
                    setParam(s, "filt_phase", v.linear ? 1.0f : 0.0f);
                    setParam(s, "sub_level", -60.0f);
                });
                REQUIRE(p != nullptr);
                const auto in = sig15(set, kFs, 0.25);
                const size_t blocks[] = {512};
                const auto out = useFloat ? render<float>(*p, in, {}, blocks) : render<double>(*p, in, {}, blocks);
                CHECK(delayedExactly(out, in, static_cast<size_t>(p->getLatencySamples())));
            }
}

TEST_CASE("T-SUM-03 Wet Only + dry only = full mix (null <= -140 dB)", "[processor]")
{
    const auto in = same(sig::bass808(kFs), 2);
    auto run = [&](const std::function<void(SubGenProcessor&)>& setup) {
        auto p = makeProcessor({}, kFs, 512, setup);
        return render(*p, in);
    };
    const auto full = run({});
    const auto wet = run([](SubGenProcessor& s) { setParam(s, "wet_only", 1.0f); });
    const auto dry = run([](SubGenProcessor& s) { setParam(s, "sub_level", -60.0f); });
    for (size_t c = 0; c < 2; ++c)
    {
        std::vector<double> sum(full[c].size());
        for (size_t i = 0; i < sum.size(); ++i)
            sum[i] = wet[c][i] + dry[c][i];
        CHECK(nullDb(full[c], sum) <= -140.0);
        CHECK(rmsDb(wet[c], 0, wet[c].size()) > -60.0); // there is a sub
    }
}

TEST_CASE("T-SUM-04 bypass equals the delayed input bit-exact in steady state, without clicks", "[processor]")
{
    const auto x = same(sig::sine(55.0, kFs, 1.0, -12.0), 2);
    auto p = makeProcessor({}, kFs, 512);
    const size_t blocks[] = {64};
    const auto out = render(*p, x, {}, blocks, [&](size_t pos) {
        if (pos == 24000)
            setParam(*p, "bypass", 1.0f);
    });
    const auto latency = static_cast<size_t>(p->getLatencySamples());
    bool exact = true;
    for (size_t i = 24000 + 480; i < x[0].size(); ++i) // after the 10 ms crossfade
        exact = exact && out[0][i] == x[0][i - latency] && out[1][i] == x[1][i - latency];
    CHECK(exact);
    const double steady = std::max(maxFirstDifference(out[0], 12000, 24000), maxFirstDifference(out[0], 30000, 48000));
    CHECK(maxFirstDifference(out[0], 24000, 25000) <= 1.5 * steady);
    // a crossfade, not a switch: over its first 5 ms the output keeps half of the sub or more (against no bypass)
    auto q = makeProcessor({}, kFs, 512);
    const auto ref = render(*q, x, {}, blocks);
    std::vector<double> kept(240), sub(240);
    for (size_t i = 0; i < 240; ++i)
    {
        kept[i] = out[0][24000 + i] - x[0][24000 + i - latency];
        sub[i] = ref[0][24000 + i] - x[0][24000 + i - latency];
    }
    const double keptDb = rmsDb(kept, 0, 240) - rmsDb(sub, 0, 240);
    CHECK(keptDb > -6.0);
    CHECK(keptDb < 0.01);
}

TEST_CASE("T-LAT-01 impulse: dry exactly L_total, TRIGGER sub +- 1 sample of L_sub", "[processor]")
{
    for (const Quality q : {Quality::Live, Quality::Mix, Quality::Print})
    {
        INFO("quality " << static_cast<int>(q));
        auto p = makeProcessor({}, kFs, 512, [&](SubGenProcessor& s) {
            setParam(s, "quality", qualityValue(q));
            setParam(s, "sub_level", -60.0f);
        });
        auto x = std::vector<double>(24000, 0.0);
        x[1000] = 0.5;
        const auto out = render(*p, same(x, 2));
        const auto latency = static_cast<size_t>(p->getLatencySamples());
        CHECK(firstAbove(out[0], 0.0) == 1000 + latency);
        CHECK(out[0][1000 + latency] == 0.5);
    }
    // the TRIGGER sub starts where the engine starts it, L_sub - E later
    const auto kick = sig::kick(kFs, {.nHits = 1, .spacing = 0.5, .levelDb = -6.0});
    auto p = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) {
        setParam(s, "engine", 2.0f); // Trigger
        setParam(s, "wet_only", 1.0f);
    });
    const auto out = render(*p, same(kick, 2));
    engines::EngineHost host;
    host.prepare(kFs, 64, Quality::Mix);
    host.setEngine(engines::EngineType::Trigger);
    host.setControl(engines::EngineControl{});
    std::vector<double> engine(kick.size());
    host.process(kick, {}, engine);
    const auto lSub = p->getLatencySamples();
    const auto e = engineLookaheadSamples(Quality::Mix, kFs);
    const auto kp = static_cast<long>(firstAbove(out[0], 1e-12)), ke = static_cast<long>(firstAbove(engine, 1e-12));
    INFO("processor " << kp << ", engine " << ke << ", L_sub " << lSub << ", E " << e);
    CHECK(std::abs((kp - ke) - (lSub - e)) <= 1);
}

TEST_CASE("T-LAT-02 latency independent of engine, Drive, Comp, EQ and sub_lp with Linear Phase", "[processor]")
{
    auto p = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) {
        setParam(s, "quality", qualityValue(Quality::Print));
        setParam(s, "filt_phase", 1.0f);
    });
    const auto base = p->getLatencySamples();
    CHECK(base == plugin::computeLatency(Quality::Print, true, 0.0, kFs, p->latencyBreakdown().os).total);
    const auto x = same(sig::bass808(kFs), 2);
    for (const auto& [id, value] : std::vector<std::pair<const char*, float>>{
             {"engine", 1.0f}, {"engine", 2.0f}, {"engine", 3.0f}, {"engine", 4.0f}, {"engine", 0.0f}, {"drive", 70.0f}, {"drive_mode", 3.0f}, {"comp_on", 1.0f},
             {"eq1_on", 1.0f}, {"eq1_gain", 9.0f}, {"sub_lp_freq", 60.0f}, {"sub_lp_slope", 3.0f}, {"drive_pos", 1.0f}})
    {
        INFO(id);
        setParam(*p, id, value);
        render(*p, x, 512);
        p->applyPendingConfiguration();
        CHECK(p->getLatencySamples() == base);
    }
}

TEST_CASE("T-INV-01 block sizes {1, 7, 32, 64, 127, 128, 512, 4096, random} within -130 dB", "[processor]")
{
    const auto x = same(sig::bass808(kFs), 2);
    auto run = [&](std::span<const size_t> blocks) {
        auto p = makeProcessor({}, kFs, 4096);
        return render(*p, x, {}, blocks);
    };
    const size_t reference[] = {512};
    const auto a = run(reference);
    std::vector<size_t> random;
    std::uint64_t s = 88172645463325252ull;
    for (int i = 0; i < 400; ++i)
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        random.push_back(1 + static_cast<size_t>(s % 4096));
    }
    for (const size_t block : {size_t{1}, size_t{7}, size_t{32}, size_t{64}, size_t{127}, size_t{128}, size_t{4096}})
    {
        INFO("blocks of " << block);
        const size_t one[] = {block};
        const auto b = run(one);
        CHECK(nullDb(a[0], b[0]) <= -130.0);
        CHECK(nullDb(a[1], b[1]) <= -130.0);
    }
    const auto b = run(random);
    CHECK(nullDb(a[0], b[0]) <= -130.0);
}

TEST_CASE("T-INV-02 44.1-192 kHz: frequency ratios and latency in ms +- 1 sample", "[processor]")
{
    for (const double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
        for (const Quality q : {Quality::Mix, Quality::Print})
        {
            INFO("fs " << fs << ", quality " << static_cast<int>(q));
            auto p = makeProcessor({}, fs, 512, [&](SubGenProcessor& s) {
                setParam(s, "quality", qualityValue(q));
                setParam(s, "wet_only", 1.0f);
            });
            // the lookahead parts are defined in ms; the oversampler's latency is fixed in samples (JUCE's filters)
            const auto br = p->latencyBreakdown();
            const double lookMs = q == Quality::Mix ? 47.0 : 65.0;
            CHECK(std::abs(static_cast<double>(br.look) - lookMs * fs / 1000.0) <= 1.0);
            CHECK(p->getLatencySamples() == br.os + br.look + br.lin + br.align);
            // DIVIDE halves a 100 Hz source at every rate
            const auto x = same(sig::sine(100.0, fs, 1.0, -12.0), 2);
            const auto y = render(*p, x);
            const auto peak = test::peakFreq(std::span<const double>(y[0]).last(static_cast<size_t>(0.5 * fs)), fs, 20.0, 200.0);
            CHECK(std::abs(peak.freqHz - 50.0) <= 0.25);
        }
}

TEST_CASE("T-INV-04 two runs are bit-identical", "[processor]")
{
    const auto x = same(sig::bass808(kFs), 2);
    auto run = [&] {
        auto p = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "drive", 40.0f); });
        return render(*p, x);
    };
    CHECK(run() == run());
}

TEST_CASE("T-ALN-01 Auto align_ms equals the analytic sub-path group delay (SPEC section 9.2 chain) +- 0.1 ms, clamped to 0-40 "
          "ms; default chain at f_sub1 30/40/50/80 Hz: 34.8/24.1/19.8/12.2 ms",
          "[processor]")
{
    auto p = makeProcessor({}, kFs, 512);
    for (const auto& [f, ms] : {std::pair{30.0, 34.8}, std::pair{40.0, 24.1}, std::pair{50.0, 19.8}, std::pair{80.0, 12.2}})
    {
        INFO(f << " Hz: " << p->estimatedSubDelayMs(f) << " ms");
        CHECK(std::abs(p->estimatedSubDelayMs(f) - ms) <= 0.1);
    }
    // other band, filter and rotation settings against the chain built here: source band at 2 f, sub filters and
    // chain A at f
    setParam(*p, "range_lo", 45.0f);
    setParam(*p, "range_hi", 200.0f);
    setParam(*p, "sub_hp_freq", 35.0f);
    setParam(*p, "sub_lp_slope", 2.0f); // 48 dB/oct
    setParam(*p, "sub_rot", 60.0f);     // the rotation's angle does not change chain A's delay
    for (const double f : {30.0, 40.0, 50.0, 80.0})
    {
        engines::SourceBand band;
        band.prepare(kFs);
        band.setRange(45.0, 200.0, 24);
        dsp::FilterCascade hp, lp;
        hp.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::HighPass, 4, 35.0, kFs));
        lp.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::LowPass, 8, 140.0, kFs));
        chain::HilbertPair pair;
        pair.prepare(kFs);
        const double expected = band.groupDelayMs(2.0 * f) + 1000.0 * (hp.groupDelaySeconds(f, kFs) + lp.groupDelaySeconds(f, kFs))
                              + pair.groupDelayMsA(f);
        INFO(f << " Hz");
        CHECK(std::abs(p->estimatedSubDelayMs(f) - expected) <= 0.1);
    }
    // the action: DIVIDE on an 80 Hz source reports f_sub1 = 40 Hz
    auto q = makeProcessor({}, kFs, 512);
    render(*q, same(sig::sine(80.0, kFs, 1.0, -12.0), 2));
    q->applyAutoAlignMs();
    const double want = std::clamp(std::round(q->estimatedSubDelayMs(q->subFrequencyHz()) * 10.0) / 10.0, 0.0, 40.0);
    CHECK(std::abs(getParam(*q, "align_ms") - want) <= 1e-4);
    CHECK(std::abs(q->subFrequencyHz() - 40.0) <= 0.5);
    // clamped to 40 ms: a slow chain
    setParam(*q, "sub_hp_freq", 60.0f);
    setParam(*q, "sub_hp_slope", 3.0f);
    setParam(*q, "range_lo", 15.0f);
    CHECK(q->estimatedSubDelayMs(20.0) > 40.0);
}

TEST_CASE("T-RT-01 no allocations in processBlock after prepare", "[processor]")
{
    for (const auto& [set, q] : {std::pair{Set::stereo(), Quality::Mix}, std::pair{Set::create5point1(), Quality::Print},
                                 std::pair{Set::create7point1point4(), Quality::Live}})
    {
        INFO(set.getDescription());
        auto p = makeProcessor({set, set}, kFs, 512, [&](SubGenProcessor& s) {
            setParam(s, "quality", qualityValue(q));
            if (q == Quality::Print)
                setParam(s, "filt_phase", 1.0f);
        });
        const auto in = sig15(set, kFs, 1.0);
        juce::AudioBuffer<double> buffer(set.size(), 512);
        juce::MidiBuffer midi;
        size_t allocations = 0;
        for (size_t pos = 0; pos + 512 <= in[0].size(); pos += 512)
        {
            for (int c = 0; c < set.size(); ++c)
                buffer.copyFrom(c, 0, in[static_cast<size_t>(c)].data() + pos, 512);
            if (pos == 9216)
            {
                setParam(*p, "engine", 1.0f);
                setParam(*p, "drive", 50.0f);
                setParam(*p, "out_mode", 3.0f);
                setParam(*p, "drive_pos", 1.0f);
                setParam(*p, "filt_type", 2.0f);
            }
            test::AllocGuard guard;
            p->processBlock(buffer, midi);
            allocations += guard.count();
        }
        CHECK(allocations == 0);
    }
}

TEST_CASE("T-RT-02 denormal ramp: no NaN, CPU within +20 %", "[processor]")
{
    auto time = [](const Channels& x) {
        std::vector<double> runs;
        Channels y;
        for (int r = 0; r < 5; ++r)
        {
            auto p = makeProcessor({}, kFs, 512);
            const auto t0 = std::chrono::steady_clock::now();
            y = render(*p, x);
            runs.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(runs.begin(), runs.end());
        return std::pair{runs[2], y};
    };
    const auto [denormal, y] = time(same(sig::denormalRamp(96000), 2));
    const auto [normal, z] = time(same(sig::sine(60.0, kFs, 2.0, -20.0), 2));
    INFO("denormal " << denormal << " s, normal " << normal << " s");
    CHECK(finite(y));
    CHECK(denormal <= 1.2 * normal);
}

TEST_CASE("T-STAB-03 SIG-09/10/11/12 stable; silence after silence <= -120 dB after 1 s", "[processor]")
{
    auto impulse = std::vector<double>(48000, 0.0);
    impulse[100] = 0.5;
    for (const auto& x : {impulse, sig::silence(kFs, 6.0), sig::dc(kFs, 1.0, 0.5), sig::square(40.0, kFs, 1.0, 0.0)})
    {
        auto p = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) { setParam(s, "wet_only", 1.0f); });
        std::vector<double> then(x);
        then.resize(x.size() + 96000, 0.0); // then 2 s of silence
        const auto y = render(*p, same(then, 2));
        CHECK(finite(y));
        double peak = 0.0;
        for (const double v : y[0])
            peak = std::max(peak, std::abs(v));
        CHECK(peak <= 1.0);
        CHECK(rmsDb(y[0], x.size() + 48000, then.size()) <= -120.0);
    }
}

TEST_CASE("T-STAB-05 changing quality, filt_phase or align_ms while playing stays finite with a mute ramp", "[processor]")
{
    auto p = makeProcessor({}, kFs, 512);
    const auto x = same(sig::sine(55.0, kFs, 0.5, -12.0), 2);
    const auto mixLatency = p->getLatencySamples();
    render(*p, x);
    setParam(*p, "quality", qualityValue(Quality::Print));
    const auto waiting = render(*p, x); // the audio thread mutes until the message thread re-prepares
    CHECK(finite(waiting));
    CHECK(rmsDb(waiting[0], 4800, waiting[0].size()) <= -200.0);
    p->applyPendingConfiguration();
    const auto printLatency = p->getLatencySamples();
    CHECK(printLatency > mixLatency);
    CHECK(p->warnings().has(Warning::LatencyChanged));
    const auto back = render(*p, x);
    CHECK(finite(back));
    CHECK(rmsDb(back[0], 0, 480) < rmsDb(back[0], 12000, 24000) - 6.0); // fading in
    CHECK(rmsDb(back[0], 12000, 24000) > -40.0);
    // align_ms and filt_phase switch on the audio thread under a mute ramp; the latency follows
    setParam(*p, "align_ms", 10.0f);
    const auto aligned = render(*p, x);
    CHECK(finite(aligned));
    p->applyPendingConfiguration();
    CHECK(p->getLatencySamples() == printLatency + 480);
    setParam(*p, "filt_phase", 1.0f);
    REQUIRE(p->waitForLinearPhase(20000));
    const auto linear = render(*p, x);
    CHECK(finite(linear));
    p->applyPendingConfiguration();
    CHECK(p->getLatencySamples() == printLatency + 480 + p->latencyBreakdown().lin);
    CHECK(p->latencyBreakdown().lin == (4097 - 1) / 2 + 256);
}

TEST_CASE("T-UI-02 warning logic W-01..W-12 except W-11", "[processor]")
{
    const auto bass = same(sig::sine(60.0, kFs, 1.0, -12.0), 2);
    auto warned = [&](Warning w, const std::function<void(SubGenProcessor&)>& setup, const Channels& in, Layout layout = {}) {
        auto p = makeProcessor(layout, kFs, 512, setup);
        REQUIRE(p != nullptr);
        render(*p, in);
        return p->warnings().has(w);
    };
    // W-01: LFE mode without an LFE channel
    CHECK(warned(Warning::NoLfeChannel, [](SubGenProcessor& s) { setParam(s, "out_mode", 4.0f); }, bass));
    CHECK_FALSE(warned(Warning::NoLfeChannel, {}, bass));
    // W-02: an external key asked for, no sidechain bus
    CHECK(warned(Warning::KeyNotConnected, [](SubGenProcessor& s) { setParam(s, "sc_use", 1.0f); }, bass));
    // W-03: Sum of an out-of-phase source
    auto anti = bass;
    for (auto& v : anti[1])
        v = -v;
    CHECK(warned(Warning::SourceOutOfPhase, [](SubGenProcessor& s) { setParam(s, "src_mode", 0.0f); }, anti));
    CHECK_FALSE(warned(Warning::SourceOutOfPhase, [](SubGenProcessor& s) { setParam(s, "src_mode", 0.0f); }, bass));
    // W-04: two sub streams that cancel in mono. DIVIDE's right stream, one input period behind the left, flips once
    // less: the two subs are in antiphase. (An inverted right channel puts them a quarter sub period apart: rho 0.)
    auto late = bass;
    std::fill(late[1].begin(), late[1].end(), 0.0);
    std::copy(bass[0].begin(), bass[0].end() - 800, late[1].begin() + 800);
    auto perChannel = [](SubGenProcessor& s) {
        setParam(s, "src_mode", 2.0f);
        setParam(s, "out_mode", 2.0f);
        setParam(s, "sub_width", 100.0f);
    };
    CHECK(warned(Warning::SubCancelsInMono, perChannel, late));
    CHECK_FALSE(warned(Warning::SubCancelsInMono, perChannel, bass));
    // W-06: output at or over 0 dBFS (the dry alone, sub Off: a -1 dBFS sine peaks at 1.12 with dry +2 dB, at 0.89 at 0 dB)
    const auto loud = same(sig::sine(60.0, kFs, 1.0, -1.0), 2);
    CHECK(warned(Warning::Clip, [](SubGenProcessor& s) {
        setParam(s, "sub_level", -60.0f);
        setParam(s, "dry_level", 2.0f);
    }, loud));
    CHECK_FALSE(warned(Warning::Clip, [](SubGenProcessor& s) { setParam(s, "sub_level", -60.0f); }, loud));
    // W-08: Linear Phase outside Print
    CHECK(warned(Warning::LinearPhaseNeedsPrint, [](SubGenProcessor& s) { setParam(s, "filt_phase", 1.0f); }, bass));
    // W-09: the confidence gate holds the sub back while the source plays
    CHECK(warned(Warning::LowConfidence, [](SubGenProcessor& s) { setParam(s, "conf_gate", 100.0f); },
                 same(sig::bandNoise(kFs, 1.0, 30.0, 120.0, -12.0), 2)));
    CHECK_FALSE(warned(Warning::LowConfidence, {}, bass));
    // W-10: exactly when sanitise() corrects the range (range_hi under 1.2 range_lo moves range_lo down), also at the
    // boundary, where the parameters' float round trip decides
    struct Range
    {
        float lo, hi;
    };
    for (const auto r : {Range{100.0f, 110.0f}, Range{30.0f, 120.0f}, Range{50.0f, 60.0f}, Range{75.0f, 90.0f},
                         Range{40.0f, 48.0f}, Range{100.0f, 120.0f}, Range{33.0f, 39.6f}, Range{25.0f, 30.0f}})
    {
        INFO("range " << r.lo << "-" << r.hi);
        auto p = makeProcessor({}, kFs, 512, [&](SubGenProcessor& s) {
            setParam(s, "range_lo", r.lo);
            setParam(s, "range_hi", r.hi);
        });
        render(*p, bass);
        params::ParamSnapshot asSet;
        params::ParamView(p->parameters()).read(asSet);
        auto corrected = asSet;
        params::sanitise(corrected);
        CHECK(p->warnings().has(Warning::RangeCorrected)
              == (corrected[params::ParamId::range_lo] != asSet[params::ParamId::range_lo]));
    }
    CHECK(warned(Warning::RangeCorrected, [](SubGenProcessor& s) {
        setParam(s, "range_lo", 100.0f);
        setParam(s, "range_hi", 110.0f);
    }, bass));
    CHECK_FALSE(warned(Warning::RangeCorrected, {}, bass));
    // W-05 and W-01 with LFE layouts: test_layouts.cpp; W-07: T-STAB-05; W-12: test_layouts.cpp
}

TEST_CASE("the sub chain follows the engine's Sub -1: Boost Follow centres a narrow boost on it", "[processor]")
{
    // a 60 Hz note: DIVIDE's Sub -1 at 30 Hz, away from base_freq (45 Hz, the chain's f_sub1 before an estimate)
    const auto x = same(sig::sine(60.0, kFs, 1.5, -18.0), 2);
    auto level = [&](float boostDb) {
        auto p = makeProcessor({}, kFs, 512, [&](SubGenProcessor& s) {
            setParam(s, "wet_only", 1.0f);
            setParam(s, "boost_gain", boostDb);
            setParam(s, "boost_q", 4.0f);
        });
        const auto y = render(*p, x);
        return rmsDb(y[0], 48000, 72000);
    };
    const double gain = level(12.0f) - level(0.0f);
    INFO("boost " << gain << " dB");
    CHECK(std::abs(gain - 12.0) < 1.0);
}

TEST_CASE("sc_use Trigger: the key's hits fire TRIGGER while the main input stays the source", "[processor]")
{
    // a silent main and a kick on the key: the sub comes from the key's hits (trig_vel 0: F-7, the velocity reads the
    // source)
    const auto kick = sig::kick(kFs, {.nHits = 2, .spacing = 0.4, .levelDb = -6.0});
    const auto silence = same(std::vector<double>(kick.size(), 0.0), 2);
    auto p = makeProcessor({Set::stereo(), Set::stereo(), Set::stereo()}, kFs, 512, [](SubGenProcessor& s) {
        setParam(s, "engine", 2.0f); // Trigger
        setParam(s, "sc_use", 2.0f); // Trigger
        setParam(s, "trig_vel", 0.0f);
    });
    REQUIRE(p != nullptr);
    const size_t blocks[] = {512};
    const auto y = render(*p, silence, same(kick, 2), blocks);
    CHECK(rmsDb(y[0], 0, y[0].size()) > -40.0);
    CHECK_FALSE(p->warnings().has(Warning::KeyNotConnected));
}

TEST_CASE("blocks larger than the prepared maximum are processed in chunks", "[processor]")
{
    const auto x = same(sig::bass808(kFs), 2);
    auto p = makeProcessor({}, kFs, 64);
    const size_t big[] = {4096};
    const auto a = render(*p, x, {}, big);
    auto q = makeProcessor({}, kFs, 64);
    const auto b = render(*q, x, 64);
    CHECK(a == b);
}

TEST_CASE("re-prepare at a new sample rate and layout keeps parameters and re-reports latency", "[processor]")
{
    auto p = makeProcessor({}, kFs, 512, [](SubGenProcessor& s) {
        setParam(s, "drive", 33.0f);
        setParam(s, "quality", qualityValue(Quality::Print));
    });
    const auto first = p->getLatencySamples();
    p->releaseResources();
    REQUIRE(p->setBusesLayout(busesOf({Set::create5point1(), Set::create5point1()})));
    p->setRateAndBufferSizeDetails(96000.0, 256);
    p->prepareToPlay(96000.0, 256);
    CHECK(std::abs(getParam(*p, "drive") - 33.0f) <= 1e-4f);
    CHECK(static_cast<int>(getParam(*p, "quality")) == 2);
    CHECK(p->getLatencySamples() == plugin::computeLatency(Quality::Print, false, 0.0, 96000.0, p->latencyBreakdown().os).total);
    CHECK(p->getLatencySamples() != first);
    const auto y = render(*p, sig15(Set::create5point1(), 96000.0, 0.3), 256);
    CHECK(finite(y));
}

TEST_CASE("NaN only in the sidechain does not reach the output", "[processor]")
{
    auto p = makeProcessor({Set::stereo(), Set::stereo(), Set::stereo()}, kFs, 512,
                           [](SubGenProcessor& s) { setParam(s, "sc_use", 1.0f); }); // Detect
    REQUIRE(p != nullptr);
    const auto main = same(sig::sine(60.0, kFs, 1.0, -12.0), 2);
    const auto key = same(sig::nanInf(48000), 2);
    const size_t blocks[] = {512};
    const auto y = render(*p, main, key, blocks);
    CHECK(finite(y));
    CHECK_FALSE(p->warnings().has(Warning::KeyNotConnected));
    // and the key's sub carries on through the bad samples: the source is sanitised before the engines (a NaN inside
    // them would cost a reset and a new onset)
    const auto latency = static_cast<size_t>(p->getLatencySamples());
    auto wetDb = [&](size_t from, size_t to) {
        double e = 0.0;
        for (size_t i = from; i < to; ++i)
            e += std::pow(y[0][i] - main[0][i - latency], 2.0);
        return 10.0 * std::log10(e / static_cast<double>(to - from) + 1e-30);
    };
    for (const size_t bad : {48000 / 4, 48000 / 2, 3 * 48000 / 4}) // +inf, -inf, NaN
    {
        const size_t at = bad + latency;
        CHECK(wetDb(at - 2400, at) > -40.0);
        CHECK(std::abs(wetDb(at, at + 2400) - wetDb(at - 2400, at)) < 3.0);
    }
}

TEST_CASE("the host's own bypass (processBlockBypassed) outputs the input delayed by the reported latency", "[processor]")
{
    auto p = makeProcessor({}, kFs, 512);
    const auto x = same(sig::bass808(kFs), 2);
    const auto latency = static_cast<size_t>(p->getLatencySamples());
    REQUIRE(latency > 0);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    bool exact = true;
    for (size_t start = 0; start + 512 <= x[0].size(); start += 512)
    {
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < 512; ++i)
                buffer.setSample(c, static_cast<int>(i), static_cast<float>(x[static_cast<size_t>(c)][start + i]));
        p->processBlockBypassed(buffer, midi);
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < 512; ++i)
            {
                const size_t at = start + i;
                const float expected = at >= latency ? static_cast<float>(x[static_cast<size_t>(c)][at - latency]) : 0.0f;
                exact = exact && buffer.getSample(c, static_cast<int>(i)) == expected;
            }
    }
    CHECK(exact);
}

TEST_CASE("5 s of digital silence reset every state: what follows renders as after a fresh prepare", "[processor]")
{
    // A note, 6 s of digital silence, the note again. The reset comes with the sub-block that completes 5 s of
    // silence; a fresh processor fed the silence left after it, then the note, renders the same, bit for bit.
    auto note = sig::bass808(kFs);
    note.resize(note.size() / 512 * 512);
    REQUIRE(note.back() != 0.0); // the silence starts right after the note, on the 64-sample grid
    const size_t silence = 563 * 512;                  // 6.005 s
    const size_t resetAt = note.size() + 240000 - 64;  // the sub-block completing 5 s
    const size_t after = note.size() + silence - resetAt; // the silence after the reset, whole sub-blocks
    for (const float engine : {0.0f, 1.0f, 2.0f, 3.0f, 4.0f})
    {
        CAPTURE(engine);
        auto setup = [engine](SubGenProcessor& s) { setParam(s, "engine", engine); };
        auto x = note;
        x.resize(note.size() + silence, 0.0);
        x.insert(x.end(), note.begin(), note.end());
        auto p = makeProcessor({}, kFs, 512, setup);
        const auto y = render(*p, same(x, 2));
        std::vector<double> z(after, 0.0);
        z.insert(z.end(), note.begin(), note.end());
        auto q = makeProcessor({}, kFs, 512, setup);
        const auto fresh = render(*q, same(z, 2), 64);
        double worst = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < note.size(); ++i)
                worst = std::max(worst, std::abs(y[c][x.size() - note.size() + i] - fresh[c][after + i]));
        CHECK(worst == 0.0);
    }
}

TEST_CASE("1e4 random automation steps of every automatable parameter stay finite and bounded", "[processor]")
{
    auto p = makeProcessor({}, kFs, 64);
    std::vector<juce::RangedAudioParameter*> automatable;
    for (auto* param : p->getParameters())
        if (param->isAutomatable())
            automatable.push_back(dynamic_cast<juce::RangedAudioParameter*>(param));
    REQUIRE(automatable.size() == 138);
    const auto x = sig::bass808(kFs);
    juce::AudioBuffer<double> buffer(2, 64);
    juce::MidiBuffer midi;
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    auto next = [&] {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    };
    double peak = 0.0;
    bool allFinite = true;
    for (int step = 0; step < 10000; ++step)
    {
        auto* param = automatable[next() % automatable.size()];
        param->setValueNotifyingHost(static_cast<float>(next() % 1001) / 1000.0f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 64; ++i)
                buffer.setSample(c, i, x[(static_cast<size_t>(step) * 64 + static_cast<size_t>(i)) % x.size()]);
        p->processBlock(buffer, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 64; ++i)
            {
                const double v = buffer.getSample(c, i);
                allFinite = allFinite && std::isfinite(v);
                peak = std::max(peak, std::abs(v));
            }
    }
    CHECK(allFinite);
    // No runaway: under the largest gain the parameters can stack with the limiter off (src +24, two subs +18,
    // sub_level +18, boost +18, auto gain +18, comp makeup and auto makeup +43, five EQ bands +90, route +6,
    // out_trim +12: +247 dB). A jump threshold in dB/ms is T-STAB-01's (Phase 5): src_mode, the route delays and
    // TRIGGER's onsets switch hard by design.
    CHECK(peak <= 1e13);
}
