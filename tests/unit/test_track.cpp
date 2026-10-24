// SPDX-License-Identifier: AGPL-3.0-or-later
// TRACK (SPEC §6.3, T-TRK-02..04): the oscillator on the pitch track, the PLL phase lock, Fixed mode and the
// lookahead split (pitch and confidence read E ahead of the output, amplitude at the output sample).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

#include "core/Quality.h"
#include "engines/SourceBand.h"
#include "engines/Track.h"
#include "support/AllocGuard.h"
#include "support/Measure.h"
#include "support/PitchEval.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::engines;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Run
{
    std::vector<double> out;        // aligned with the input (the lookahead removed)
    std::vector<TrackProbe> probe;  // aligned with the input
    double onsetDelayMs = kNaN;
    double noteBeginMs = kNaN; // the engine's note start, ms from 0.2 s
};

Run runTrack(const std::vector<double>& x, Quality quality, EngineControl control = {}, double fs = kFs)
{
    Track track;
    const int look = engineLookaheadSamples(quality, fs);
    track.prepare(fs, 1, look);
    track.setControl(control);
    const size_t total = x.size() + static_cast<size_t>(look);
    std::vector<double> rawOut(total);
    std::vector<TrackProbe> rawProbe(total);
    const std::vector<double> silence(1, 0.0);
    for (size_t n = 0; n < total; ++n)
    {
        const double in = n < x.size() ? x[n] : 0.0;
        track.process(std::span<const double>(&in, 1), silence, std::span<double>(&rawOut[n], 1));
        rawProbe[n] = track.probe();
    }
    Run r;
    r.out.assign(rawOut.begin() + look, rawOut.end());
    r.probe.assign(rawProbe.begin() + look, rawProbe.end());
    r.onsetDelayMs = track.onsetDelayMs();
    r.noteBeginMs = (static_cast<double>(track.lastNoteBegin()) - 0.2 * fs) / fs * 1000.0;
    return r;
}

EngineControl withPhase(PhaseGen mode, double speed = 0.5)
{
    EngineControl c;
    c.phaseGen = mode;
    c.trkSpeed = speed;
    return c;
}

std::vector<double> afterSilence(const std::vector<double>& note, double fs = kFs)
{
    auto x = sig::silence(fs, 0.2);
    x.insert(x.end(), note.begin(), note.end());
    return x;
}

// The sub's frequency x 2 (the source pitch it implies) every 2 ms, scored like the detector.
subgen::test::PitchTrack sourcePitchOfSub(const Run& r, double fs = kFs)
{
    subgen::test::PitchTrack trk;
    for (size_t i = 0; i < r.probe.size(); i += static_cast<size_t>(std::lround(0.002 * fs)))
    {
        trk.t.push_back(static_cast<double>(i) / fs);
        trk.f0.push_back(r.probe[i].subFreq > 0.0 ? 2.0 * r.probe[i].subFreq : kNaN);
        trk.conf.push_back(r.probe[i].conf);
    }
    return trk;
}

double truthGlide(double t) { return 80.0 * std::pow(2.0, -std::clamp((t - 0.1) / 0.2, 0.0, 1.0)); }

// The engine's input is x_b: the glide as the default source band delays it (its group delay, 6.6 ms at 80 Hz and
// 14.6 ms at 40 Hz, is natural and left to align_ms, SPEC §6.2, §9.2).
double truthGlideOnXb(double t)
{
    static const SourceBand band = [] {
        SourceBand b;
        b.prepare(kFs);
        b.setRange(30.0, 120.0, 24);
        return b;
    }();
    return truthGlide(t - band.groupDelayMs(truthGlide(t)) / 1000.0);
}

// The source's onset as the engine sees it: the first sample of x_b (default band) above 10 % of its peak in the
// 100 ms after it, in ms from 0.2 s.
double xbOnsetMs(const std::vector<double>& x, double fs = kFs)
{
    SourceBand band;
    band.prepare(fs);
    band.setRange(30.0, 120.0, 24);
    std::vector<double> xb(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        xb[i] = band.process(x[i]);
    const auto i0 = static_cast<size_t>(0.2 * fs);
    double peak = 0.0;
    for (size_t i = i0; i < i0 + static_cast<size_t>(0.1 * fs); ++i)
        peak = std::max(peak, std::abs(xb[i]));
    for (size_t i = i0; i < xb.size(); ++i)
        if (std::abs(xb[i]) > 0.1 * peak)
            return (static_cast<double>(i) - static_cast<double>(i0)) / fs * 1000.0;
    return kNaN;
}

// The detector's pitch as the engine reads it (probe.f0) every 2 ms.
subgen::test::PitchTrack readPitch(const Run& r, double fs = kFs)
{
    subgen::test::PitchTrack trk;
    for (size_t i = 0; i < r.probe.size(); i += static_cast<size_t>(std::lround(0.002 * fs)))
    {
        trk.t.push_back(static_cast<double>(i) / fs);
        trk.f0.push_back(r.probe[i].pitchValid ? r.probe[i].f0 : kNaN);
        trk.conf.push_back(r.probe[i].conf);
    }
    return trk;
}

std::vector<double> glideSignal(double fs = kFs) // P2-03: 80 Hz, from 0.1 s a 200 ms log glide to 40 Hz
{
    std::vector<double> x(static_cast<size_t>(0.6 * fs));
    double cycles = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        cycles += truthGlide(static_cast<double>(i) / fs) / fs;
        x[i] = 0.25 * std::sin(2.0 * std::numbers::pi * cycles);
    }
    return x;
}

// First sample at which the applied envelope (amplitude x mute x gate x fade-in) passes 10 % of its value 100 ms after
// the source onset at 0.2 s, in ms from that onset.
double envelopeOnsetMs(const Run& r, double fs = kFs)
{
    const auto i0 = static_cast<size_t>(0.2 * fs);
    const double ref = r.probe[i0 + static_cast<size_t>(0.1 * fs)].envelope;
    for (size_t i = 0; i < r.probe.size(); ++i)
        if (r.probe[i].envelope > 0.1 * ref)
            return (static_cast<double>(i) - static_cast<double>(i0)) / fs * 1000.0;
    return kNaN;
}

// First sample at which the sub is ready (pitch known, gate open), in ms from 0.2 s.
double readyMs(const Run& r, double fs = kFs)
{
    for (size_t i = 0; i < r.probe.size(); ++i)
        if (r.probe[i].ready)
            return (static_cast<double>(i) - 0.2 * fs) / fs * 1000.0;
    return kNaN;
}

double wrapPi(double x) { return x - 2.0 * std::numbers::pi * std::floor((x + std::numbers::pi) / (2.0 * std::numbers::pi)); }

double dftMagnitude(std::span<const double> x, double f, double fs)
{
    std::complex<double> sum = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
        sum += x[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i) / fs);
    return std::abs(sum);
}
} // namespace

TEST_CASE("T-TRK-02 glide 80->40 Hz in 200 ms: tracking lag <= 50 ms (W = 83.5 ms)", "[track]")
{
    // range_lo 30 Hz: W = ceil(2.5 * 2000 / 30) = 167 samples = 83.5 ms; lags against the glide on x_b. Live: the
    // detector as the engine reads it (P2-03: 43 ms = W/2 + 1 ms) and Locked, whose loop follows the source itself;
    // Mix reads the pitch 45 ms ahead, so even Free with the default trk_speed smoothing (20 ms) stays inside
    const auto x = glideSignal();
    {
        INFO("Live, the detector");
        const auto score = subgen::test::evaluatePitch(readPitch(runTrack(x, Quality::Live)), truthGlideOnXb, 0.0);
        INFO("lag " << 1000.0 * score.lag << " ms, median error " << score.centsMedian << " cents");
        CHECK(score.lag <= 0.050);
        CHECK(score.octaveErrors == 0);
    }
    struct Case
    {
        const char* name;
        Quality quality;
        EngineControl control;
    };
    const Case cases[] = {
        {"Live, Locked", Quality::Live, withPhase(PhaseGen::Locked)},
        {"Mix, Locked", Quality::Mix, withPhase(PhaseGen::Locked)},
        {"Mix, Free, trk_speed 50 %", Quality::Mix, withPhase(PhaseGen::Free)},
        {"Print, Free, trk_speed 50 %", Quality::Print, withPhase(PhaseGen::Free)},
    };
    for (const auto& c : cases)
    {
        INFO(c.name);
        const auto score =
            subgen::test::evaluatePitch(sourcePitchOfSub(runTrack(x, c.quality, c.control)), truthGlideOnXb, 0.0);
        INFO("lag " << 1000.0 * score.lag << " ms, median error " << score.centsMedian << " cents");
        CHECK(score.lag <= 0.050);
        CHECK(score.octaveErrors == 0);
    }
}

TEST_CASE("T-TRK-03 sub onset within +-2 ms of the source onset in Mix (notes >= 55 Hz) and Print (notes >= 41.2 Hz); "
          "Live reports its delay",
          "[track]")
{
    // The sub is aligned when it is ready (pitch known, gate open) by 2 ms after the source's onset on x_b (10 % of
    // its peak; the band's own delay is natural, SPEC §6.2): its amplitude follows the source from there. This is
    // the budget P2-03 set the lookahead by (confidence open + decimation). The gate's 5 ms attack then fades the sub
    // in: the envelope's 10 % point is reported for information.
    auto offsetMs = [](const std::vector<double>& x, Quality q) {
        const auto r = runTrack(x, q);
        const double source = xbOnsetMs(x);
        UNSCOPED_INFO("x_b onset " << source << " ms after the raw onset; ready " << readyMs(r) - source
                                   << " ms, envelope at 10 % " << envelopeOnsetMs(r) - source << " ms after it");
        return readyMs(r) - source;
    };
    for (const double f : {55.0, 65.0, 80.0, 100.0, 120.0})
    {
        INFO("Mix, sine " << f << " Hz");
        CHECK(offsetMs(afterSilence(sig::sine(f, kFs, 0.6, -12.0)), Quality::Mix) <= 2.0);
    }
    for (const double f : {41.2, 45.0, 55.0, 80.0, 120.0})
    {
        INFO("Print, sine " << f << " Hz");
        CHECK(offsetMs(afterSilence(sig::sine(f, kFs, 0.6, -12.0)), Quality::Print) <= 2.0);
    }
    for (const double f : {55.0, 65.4, 82.4}) // SIG-04 notes A1, C2, E2 (octave glide from 2 f)
    {
        INFO("Mix, 808 " << f << " Hz");
        CHECK(offsetMs(afterSilence(sig::bass808(kFs, {.fEnd = f, .glideFrom = 2.0 * f})), Quality::Mix) <= 2.0);
    }
    for (const double f : {41.2, 55.0, 80.0}) // Live: late by about two periods, and says so
    {
        INFO("Live, sine " << f << " Hz");
        const auto x = afterSilence(sig::sine(f, kFs, 0.6, -12.0));
        const auto r = runTrack(x, Quality::Live);
        const double late = readyMs(r) - xbOnsetMs(x);
        const double fromNoteStart = envelopeOnsetMs(r) - r.noteBeginMs; // what the report measures
        INFO("late " << late << " ms on x_b; " << fromNoteStart << " ms from the note start, reported "
                     << r.onsetDelayMs << " ms");
        CHECK(late >= 1500.0 / f);
        CHECK(std::abs(fromNoteStart - r.onsetDelayMs) <= 2.0);
    }
}

TEST_CASE("T-TRK-04 Locked steady-state phase error <= 15 deg rms, clean and at in-band SNR 10 dB", "[track]")
{
    // the error 2 phi_sub - phi_in, phi_in = the phase of the clean tone through the same source band, fitted as
    // B cos(w t + theta) over 1-3 s
    const double f = 55.0;
    const auto clean = sig::sine(f, kFs, 3.0, -12.0);
    SourceBand band;
    band.prepare(kFs);
    band.setRange(30.0, 120.0, 24);
    const double w = 2.0 * std::numbers::pi * f / kFs;
    double c = 0.0;
    double s = 0.0;
    for (size_t i = 0; i < clean.size(); ++i)
    {
        const double xb = band.process(clean[i]);
        if (i >= 48000)
        {
            c += xb * std::cos(w * static_cast<double>(i));
            s += xb * std::sin(w * static_cast<double>(i));
        }
    }
    const double theta = std::atan2(-s, c);
    auto noisy = clean;
    const auto noise = sig::bandNoise(kFs, 3.0, 30.0, 120.0, 0.0, 7);
    const double scale = (test::rms(clean) / std::pow(10.0, 10.0 / 20.0)) / test::rms(noise);
    for (size_t i = 0; i < noisy.size(); ++i)
        noisy[i] += noise[i] * scale;
    for (const auto& [name, input] : {std::pair{"clean", clean}, std::pair{"SNR 10 dB", noisy}})
        for (const Quality q : {Quality::Live, Quality::Mix})
        {
            INFO(name << (q == Quality::Live ? ", Live" : ", Mix"));
            const auto r = runTrack(input, q);
            double sum = 0.0;
            int count = 0;
            for (size_t i = 48000; i < 144000; ++i)
            {
                const double e = wrapPi(2.0 * r.probe[i].phase1 - (w * static_cast<double>(i) + theta));
                sum += e * e;
                ++count;
            }
            const double rmsDeg = std::sqrt(sum / count) * 180.0 / std::numbers::pi;
            INFO("phase error " << rmsDeg << " deg rms");
            CHECK(rmsDeg <= 15.0);
        }
}

TEST_CASE("triangle aliasing < -90 dB for f <= 100 Hz at 44.1 kHz", "[track]")
{
    // Fixed 100 Hz, shape 100 %, Constant: 100 periods from 0.5 s; fs = 441 f puts the triangle's odd harmonics on
    // exact bins and its aliases on the even ones
    const double fs = 44100.0;
    EngineControl c;
    c.baseFixed = true;
    c.baseFreqHz = 100.0;
    c.shape = 1.0;
    c.follow = false;
    const auto r = runTrack(sig::sine(80.0, fs, 2.0, -6.0), Quality::Live, c, fs);
    const std::span<const double> seg(r.out.data() + 22050, 44100);
    const double fundamental = dftMagnitude(seg, 100.0, fs);
    double worstAlias = 0.0;
    for (int k = 2; k <= 220; k += 2)
        worstAlias = std::max(worstAlias, dftMagnitude(seg, 100.0 * k, fs));
    CHECK(20.0 * std::log10(worstAlias / fundamental) < -90.0);
    CHECK(std::abs(dftMagnitude(seg, 300.0, fs) / fundamental - 1.0 / 9.0) < 1e-5); // a triangle: odd 1/k^2
}

TEST_CASE("Fixed mode is a gated drone at base_freq", "[track]")
{
    EngineControl c;
    c.baseFixed = true;
    c.baseFreqHz = 45.0;
    c.follow = false;
    auto x = sig::sine(80.0, kFs, 0.6, -12.0); // 0.6 s of source, then 0.6 s of silence
    x.resize(57600, 0.0);
    const double level = std::pow(10.0, -12.0 / 20.0);
    {
        const auto r = runTrack(x, Quality::Mix, c);
        const std::span<const double> seg(r.out.data() + 9600, 16800);
        CHECK(std::abs(test::zcFreq(seg, kFs) / 45.0 - 1.0) <= 1e-3);
        double peak = 0.0;
        for (const double v : seg)
            peak = std::max(peak, std::abs(v));
        CHECK(std::abs(20.0 * std::log10(peak / level)) <= 0.5);
        // the activity ends when its 20 ms RMS is 3 dB under det_thresh (~250 ms after this source), then 20 ms
        double tail = 0.0;
        for (size_t i = 48000; i < r.out.size(); ++i)
            tail = std::max(tail, std::abs(r.out[i]));
        CHECK(20.0 * std::log10(tail / level) <= -60.0);
        CHECK(r.out[0] == 0.0); // nothing before the activity
    }
    c.sub1Gain = 0.0;
    c.sub2Gain = 1.0;
    const auto r2 = runTrack(x, Quality::Mix, c);
    CHECK(std::abs(test::zcFreq(std::span<const double>(r2.out.data() + 9600, 16800), kFs) / 22.5 - 1.0) <= 1e-3);
}

TEST_CASE("amplitude is not read ahead: the sub does not start before the note", "[track]")
{
    for (const Quality q : {Quality::Mix, Quality::Print})
    {
        INFO((q == Quality::Mix ? "Mix" : "Print"));
        const auto r = runTrack(afterSilence(sig::sine(55.0, kFs, 0.5, -12.0)), q);
        const auto i0 = static_cast<size_t>(0.2 * kFs);
        double before = 0.0;
        double after = 0.0;
        for (size_t i = 0; i < i0; ++i)
            before = std::max(before, std::abs(r.out[i]));
        for (size_t i = i0; i < i0 + 4800; ++i)
            after = std::max(after, std::abs(r.out[i]));
        CHECK(before == 0.0);
        CHECK(after > 0.1);
        // the pitch, read 45/55 ms ahead, is known when the note arrives on x_b
        const auto onset = i0 + static_cast<size_t>(std::lround(xbOnsetMs(afterSilence(sig::sine(55.0, kFs, 0.5, -12.0))) * kFs / 1000.0));
        CHECK(r.probe[onset].pitchValid);
    }
}

TEST_CASE("no pitch before the first valid frame: the oscillator stays silent until the first estimate", "[track]")
{
    // D8 M13: the prototype used to take the first estimate from the future
    for (const PhaseGen mode : {PhaseGen::Locked, PhaseGen::Free})
    {
        const auto r = runTrack(sig::sine(55.0, kFs, 0.5, -12.0), Quality::Live, withPhase(mode));
        size_t first = r.probe.size();
        for (size_t i = 0; i < r.probe.size() && first == r.probe.size(); ++i)
            if (r.probe[i].pitchValid)
                first = i;
        REQUIRE(first < r.probe.size());
        CHECK(static_cast<double>(first) >= 2.0 * kFs / 55.0); // two periods of the note
        int sounding = 0;
        for (size_t i = 0; i < first; ++i)
            sounding += r.out[i] != 0.0 || r.probe[i].subFreq != 0.0 ? 1 : 0;
        CHECK(sounding == 0);
        double later = 0.0;
        for (size_t i = first; i < r.out.size(); ++i)
            later = std::max(later, std::abs(r.out[i]));
        CHECK(later > 0.1);
    }
}

TEST_CASE("TRACK clamps out-of-range controls instead of failing", "[track]")
{
    EngineControl c;
    c.rangeLoHz = 0.0;
    c.rangeHiHz = std::numeric_limits<double>::quiet_NaN();
    c.trkSpeed = 9.0;
    c.trkGlideMs = -1.0;
    c.shape = std::numeric_limits<double>::infinity();
    c.envReleaseMs = 0.0;
    c.confGate = -3.0;
    const auto r = runTrack(sig::sine(55.0, kFs, 0.5, -12.0), Quality::Mix, c);
    bool finite = true;
    for (const double v : r.out)
        finite = finite && std::isfinite(v);
    CHECK(finite);
}

TEST_CASE("TRACK process does not allocate and does not depend on the block size", "[track]")
{
    const auto x = afterSilence(sig::bass808(kFs, {.fEnd = 55.0, .glideFrom = 110.0}));
    EngineControl c;
    c.sub2Gain = 0.5;
    c.trkGlideMs = 30.0;
    for (const PhaseGen mode : {PhaseGen::Locked, PhaseGen::Free, PhaseGen::Reset})
    {
        c.phaseGen = mode;
        auto run = [&](size_t block) {
            Track track;
            track.prepare(kFs, static_cast<int>(block), engineLookaheadSamples(Quality::Mix, kFs));
            track.setControl(c);
            std::vector<double> out(x.size());
            const std::vector<double> key(block, 0.0);
            size_t allocations = 0;
            for (size_t i = 0; i < x.size(); i += block)
            {
                const size_t len = std::min(block, x.size() - i);
                test::AllocGuard guard;
                track.process(std::span<const double>(x.data() + i, len), std::span<const double>(key.data(), len),
                              std::span<double>(out.data() + i, len));
                allocations += guard.count();
            }
            CHECK(allocations == 0);
            return out;
        };
        const auto a = run(1);
        const auto b = run(64);
        const auto d = run(997);
        CHECK(a == b);
        CHECK(a == d);
    }
}
