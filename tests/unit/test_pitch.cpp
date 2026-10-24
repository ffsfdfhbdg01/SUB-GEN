// SPDX-License-Identifier: AGPL-3.0-or-later
// TRACK's pitch detector (SPEC §6.3, T-TRK-01): MPM on onset-aware frames of the decimated source band and the
// continuity prior, against the prototype (tests/golden/pitch.sgg, proto/subgen_proto/track.py) and the TEST_PLAN
// criteria.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "engines/PitchMpm.h"
#include "engines/PitchPrior.h"
#include "engines/SourceBand.h"
#include "support/Golden.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::engines;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kCents05Pct = 8.7; // 0.5 % = 8.66 cents

struct Track
{
    std::vector<double> samples; // frame times in input samples
    std::vector<double> t;       // s: when each estimate becomes available
    std::vector<double> f0Raw;   // Hz, NaN without an estimate
    std::vector<double> conf;
    std::vector<double> f0;      // after the prior
};

// The detector over x in blocks of `block` samples, then the prior over its frames.
Track detect(const std::vector<double>& x, double fs, double fMin, int block = 512)
{
    PitchMpm mpm;
    mpm.prepare(fs, 300.0);
    mpm.setMinFrequency(fMin);
    PitchPrior prior;
    Track trk;
    std::vector<PitchFrame> frames(static_cast<size_t>(mpm.maxFrames(block)));
    for (size_t i = 0; i < x.size(); i += static_cast<size_t>(block))
    {
        const size_t n = std::min(static_cast<size_t>(block), x.size() - i);
        const int count = mpm.process(std::span<const double>(x.data() + i, n), frames);
        for (int k = 0; k < count; ++k)
        {
            const auto& fr = frames[static_cast<size_t>(k)];
            trk.samples.push_back(fr.timeSamples);
            trk.t.push_back(fr.timeSamples / fs);
            trk.f0Raw.push_back(fr.f0Hz);
            trk.conf.push_back(fr.confidence);
            trk.f0.push_back(prior.update(fr.f0Hz, fr.confidence));
        }
    }
    return trk;
}

std::vector<double> sourceBand(const std::vector<double>& x, double fs, double lo, double hi)
{
    SourceBand band;
    band.prepare(fs);
    band.setRange(lo, hi, 24);
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = band.process(x[i]);
    return y;
}

double median(std::vector<double> v) // numpy.median
{
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double midiHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

using Truth = std::function<double(double)>; // Hz at time t (s); NaN where no note sounds

Truth constant(double f)
{
    return [f](double) { return f; };
}

Truth truth808(double fEnd, double glideFrom)
{
    const double tau = 0.08 / 3.0;
    return [=](double t) { return t >= 0.0 && t < 0.8 ? fEnd + (glideFrom - fEnd) * std::exp(-std::max(t, 0.0) / tau) : kNaN; };
}

struct Score
{
    double centsMedian = kNaN;
    int octaveErrors = 0;
    double lockTime = kNaN;
    double lag = 0.0;
    double confOpen = kNaN;
};

// track.evaluate from `onset` on: the truth is shifted by the lag that best explains the track where it moves faster
// than 500 cents/s; median |error| from the lock (3 frames in a row within 50 cents); octave errors = frames with
// conf >= 0.5 more than 600 cents off; confOpen = the first frame with conf >= 0.5.
Score evaluate(const Track& trk, const Truth& truth, double onset)
{
    std::vector<double> t, f, conf;
    for (size_t i = 0; i < trk.t.size(); ++i)
        if (trk.t[i] >= onset - 1e-12)
        {
            t.push_back(trk.t[i]);
            f.push_back(trk.f0[i]);
            conf.push_back(trk.conf[i]);
        }
    Score s;
    if (t.empty())
        return s;
    auto errorCents = [&](double lag) {
        std::vector<double> e(t.size(), kNaN);
        for (size_t i = 0; i < t.size(); ++i)
        {
            const double tr = truth(std::max(t[i] - lag, onset));
            if (std::isfinite(tr) && tr > 0.0 && std::isfinite(f[i]) && f[i] > 0.0)
                e[i] = 1200.0 * std::log2(f[i] / tr);
        }
        return e;
    };
    const double h = 0.005;
    std::vector<bool> moving(t.size());
    int movingCount = 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        const double rate = std::abs(1200.0 * std::log2(truth(t[i] + h) / truth(t[i] - h))) / (2.0 * h);
        moving[i] = std::isfinite(rate) && rate >= 500.0;
        movingCount += moving[i] ? 1 : 0;
    }
    if (movingCount >= 5)
    {
        double best = std::numeric_limits<double>::infinity();
        for (int k = 0; k <= 500; ++k) // 0 ... 250 ms in 0.5 ms steps
        {
            const double tau = k * 0.0005;
            const auto e = errorCents(tau);
            double sum = 0.0;
            int count = 0;
            for (size_t i = 0; i < e.size(); ++i)
                if (moving[i] && std::isfinite(e[i]))
                {
                    sum += std::abs(e[i]);
                    ++count;
                }
            if (count >= 5 && sum / count < best - 1e-9)
            {
                best = sum / count;
                s.lag = tau;
            }
        }
    }
    const auto e = errorCents(s.lag);
    auto good = [&](size_t i) { return std::isfinite(e[i]) && std::abs(e[i]) <= 50.0; };
    size_t start = 0;
    bool locked = false;
    for (size_t i = 0; i + 3 <= e.size() && !locked; ++i)
        if (good(i) && good(i + 1) && good(i + 2))
        {
            start = i;
            locked = true;
        }
    if (locked)
        s.lockTime = t[start] - onset;
    std::vector<double> post;
    for (size_t i = start; i < e.size(); ++i)
        if (std::isfinite(e[i]))
            post.push_back(std::abs(e[i]));
    if (!post.empty())
        s.centsMedian = median(post);
    for (size_t i = 0; i < e.size(); ++i)
        s.octaveErrors += std::isfinite(e[i]) && conf[i] >= 0.5 && std::abs(e[i]) > 600.0 ? 1 : 0;
    for (size_t i = 0; i < conf.size(); ++i)
        if (conf[i] >= 0.5)
        {
            s.confOpen = t[i] - onset;
            break;
        }
    return s;
}

std::vector<double> afterSilence(double f, double fs)
{
    auto x = sig::silence(fs, 0.2);
    const auto tone = sig::sine(f, fs, 0.5, -12.0);
    x.insert(x.end(), tone.begin(), tone.end());
    return x;
}

bool sameOrBothNaN(double a, double b, double relTol)
{
    if (std::isnan(a) || std::isnan(b))
        return std::isnan(a) && std::isnan(b);
    return std::abs(a - b) <= relTol * std::abs(b);
}
} // namespace

TEST_CASE("T-TRK-01 SIG-01/04/05: median error <= 0.5 % and 0 octave errors (conf >= 0.5 and |err| > 600 cents) at "
          "44.1 and 48 kHz",
          "[pitch]")
{
    // P2-03's conditions: source band 25-250 Hz (SIG-01 reaches 200 Hz), search floor 25 Hz
    for (const double fs : {44100.0, 48000.0})
    {
        struct Case
        {
            std::string name;
            std::vector<double> x;
            Truth truth;
        };
        std::vector<Case> cases;
        for (const double f : {30.0, 40.0, 55.0, 80.0, 120.0, 200.0})
            for (const double level : {-6.0, -18.0, -40.0})
                cases.push_back({"SIG-01 " + std::to_string(f) + " Hz " + std::to_string(level) + " dBFS",
                                 sig::sine(f, fs, 2.0, level), constant(f)});
        for (int note = 28; note < 48; ++note) // E1 ... B2, octave glide from 2 f, 2nd harmonic -8 dB
        {
            const double f = midiHz(note);
            cases.push_back({"SIG-04 808 note " + std::to_string(note),
                             sig::bass808(fs, {.fEnd = f, .glideFrom = 2.0 * f}), truth808(f, 2.0 * f)});
        }
        for (int note = 28; note < 44; ++note)
            cases.push_back({"SIG-05 pluck note " + std::to_string(note), sig::bassPluck(fs, midiHz(note)),
                             constant(midiHz(note))});
        for (const auto& c : cases)
        {
            INFO("fs " << fs << ", " << c.name);
            const auto s = evaluate(detect(sourceBand(c.x, fs, 25.0, 250.0), fs, 25.0), c.truth, 0.0);
            CHECK(s.centsMedian <= kCents05Pct);
            CHECK(s.octaveErrors == 0);
            CHECK(std::isfinite(s.lockTime));
        }
    }
}

TEST_CASE("44.1 kHz decimates by 22 (fs_d = 2004.545 Hz)", "[pitch]")
{
    PitchMpm mpm;
    mpm.prepare(44100.0, 300.0);
    CHECK(mpm.decimation() == 22);
    CHECK(std::abs(mpm.decimatedRate() - 44100.0 / 22.0) < 1e-9);
    mpm.prepare(48000.0, 300.0);
    CHECK(mpm.decimation() == 24);
    CHECK(mpm.decimatedRate() == 2000.0);
    for (const double fs : {22050.0, 32000.0, 88200.0, 96000.0, 176400.0, 192000.0})
    {
        INFO("fs " << fs);
        mpm.prepare(fs, 300.0);
        CHECK(mpm.decimatedRate() >= 2000.0 - 1e-9);
        CHECK(mpm.decimatedRate() <= 2100.0);
        CHECK(std::abs(mpm.decimation() * mpm.decimatedRate() - fs) < 1e-6);
    }
    // frames 2 ms apart: 4 decimated samples; W = ceil(2.5 fs_d / max(range_lo, 20)): 250 at 48 kHz for 20 Hz
    mpm.prepare(48000.0, 300.0);
    mpm.setMinFrequency(20.0);
    CHECK(mpm.windowSamples() == 250);
    mpm.setMinFrequency(40.0);
    CHECK(mpm.windowSamples() == 125);
    const auto trk = detect(sig::sine(55.0, kFs, 1.0, -12.0), kFs, 20.0, 333);
    REQUIRE(trk.samples.size() == 500);
    for (size_t i = 0; i < trk.samples.size(); ++i)
        CHECK(trk.samples[i] == 96.0 * static_cast<double>(i));
}

TEST_CASE("decimation is causal: silence before a note stays exactly 0 after the anti-alias filter", "[pitch]")
{
    // D8 M13: the zero-phase resampling used before rang ahead of the signal
    std::vector<double> x(9600, 0.0);
    const auto tone = sig::sine(55.0, kFs, 0.1, -6.0);
    std::copy(tone.begin(), tone.begin() + 4800, x.begin() + 4800);
    PitchMpm mpm;
    mpm.prepare(kFs, 300.0);
    mpm.setMinFrequency(20.0);
    std::vector<PitchFrame> frames(static_cast<size_t>(mpm.maxFrames(1)));
    int nonZeroBefore = 0;
    int estimatesBefore = 0;
    bool nonZeroAfter = false;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const int count = mpm.process(std::span<const double>(&x[i], 1), frames);
        if (i % 24 == 0)
        {
            if (i < 4800)
                nonZeroBefore += mpm.lastDecimated() != 0.0 ? 1 : 0;
            else
                nonZeroAfter = nonZeroAfter || mpm.lastDecimated() != 0.0;
        }
        for (int k = 0; k < count; ++k)
        {
            const auto& fr = frames[static_cast<size_t>(k)];
            estimatesBefore += fr.timeSamples < 4800.0 && (!std::isnan(fr.f0Hz) || fr.confidence != 0.0) ? 1 : 0;
        }
    }
    CHECK(nonZeroBefore == 0);
    CHECK(nonZeroAfter);
    CHECK(estimatesBefore == 0);
}

TEST_CASE("confidence reaches 0.5 within 2 periods + 4 ms of a note after silence (30-120 Hz)", "[pitch]")
{
    for (const double fs : {44100.0, 48000.0})
        for (const double f : {30.0, 41.2, 55.0, 80.0, 120.0})
        {
            INFO("fs " << fs << " f " << f);
            const auto s = evaluate(detect(afterSilence(f, fs), fs, 20.0), [f](double t) { return t >= 0.2 ? f : kNaN; },
                                    0.2);
            CHECK(s.confOpen > 1.5 / f); // not before 1.5 periods
            CHECK(s.confOpen <= 2.0 / f + 0.004);
            CHECK(s.octaveErrors == 0);
        }
}

TEST_CASE("band-limited pink noise has median confidence <= 0.1", "[pitch]")
{
    // its raw NSDF clarity is 0.45-0.56: the calibration keeps it at 0 while a sine sits at 1
    const auto noise = detect(sig::bandNoise(kFs, 3.0, 30.0, 120.0, -12.0, 1), kFs, 30.0);
    CHECK(median({noise.conf.begin() + static_cast<std::ptrdiff_t>(noise.conf.size() / 4), noise.conf.end()}) <= 0.1);
    const auto tone = detect(sig::sine(55.0, kFs, 1.0, -12.0), kFs, 20.0);
    CHECK(median({tone.conf.begin() + static_cast<std::ptrdiff_t>(tone.conf.size() / 4), tone.conf.end()}) >= 0.95);
}

TEST_CASE("a lone outlier cannot seed the track; an octave jump needs 3 agreeing frames", "[pitch]")
{
    // track.apply_prior's cases: the track starts at the third consistent frame, so frames 0 and 1 are NaN
    auto run = [](const std::vector<double>& f, const std::vector<double>& conf) {
        PitchPrior prior;
        std::vector<double> out;
        for (size_t i = 0; i < f.size(); ++i)
            out.push_back(prior.update(f[i], conf[i]));
        return out;
    };
    const std::vector<double> ones(20, 1.0);
    {
        std::vector<double> f(12, 55.0);
        f[6] = 110.0; // a single octave outlier is rejected
        const auto out = run(f, ones);
        CHECK(std::isnan(out[0]));
        CHECK(std::isnan(out[1]));
        for (size_t i = 2; i < 12; ++i)
            CHECK(out[i] == 55.0);
    }
    {
        std::vector<double> f(6, 55.0); // an octave jump is taken on its third consistent frame
        f.insert(f.end(), 6, 110.0);
        const auto out = run(f, ones);
        for (size_t i = 2; i < 8; ++i)
            CHECK(out[i] == 55.0);
        for (size_t i = 8; i < 12; ++i)
            CHECK(out[i] == 110.0);
    }
    {
        const std::vector<double> f{55, 55, 55, 110, 30, 110, 30, 110, 55, 55}; // disagreeing outliers never add up
        const auto out = run(f, ones);
        for (size_t i = 2; i < 10; ++i)
            CHECK(out[i] == 55.0);
    }
    {
        std::vector<double> f(6, 55.0); // five unvoiced frames forget the track; the new note needs 3 frames
        f.insert(f.end(), 6, 300.0);
        f.insert(f.end(), 5, 110.0);
        std::vector<double> conf(6, 1.0);
        conf.insert(conf.end(), 6, 0.1);
        conf.insert(conf.end(), 5, 1.0);
        const auto out = run(f, conf);
        for (size_t i = 6; i < 14; ++i)
            CHECK(out[i] == 55.0);
        for (size_t i = 14; i < 17; ++i)
            CHECK(out[i] == 110.0);
    }
    {
        const std::vector<double> f{96.0, 185.0, 182.0, 180.0, 178.0, 176.0}; // a lone first frame seeds nothing
        const auto out = run(f, ones);
        for (size_t i = 0; i < 3; ++i)
            CHECK(std::isnan(out[i]));
        for (size_t i = 3; i < 6; ++i)
            CHECK(out[i] == f[i]);
    }
    {
        std::vector<double> f(100); // a smooth glide (0.5 % per frame) passes untouched
        for (size_t i = 0; i < f.size(); ++i)
            f[i] = 80.0 * std::pow(0.995, static_cast<double>(i));
        const auto out = run(f, std::vector<double>(100, 1.0));
        for (size_t i = 2; i < 100; ++i)
            CHECK(out[i] == f[i]);
    }
}

TEST_CASE("golden f0/confidence tracks within 1e-6 relative", "[pitch]")
{
    const auto golden = test::Golden::load("pitch.sgg");
    struct Case
    {
        const char* name;
        std::vector<double> input;
    };
    const std::vector<Case> cases{
        {"bass808_a1", sig::bass808(kFs, {.fEnd = 55.0, .glideFrom = 110.0})},
        {"pluck_e1", sig::bassPluck(kFs, 41.2)},
    };
    for (const auto& c : cases)
    {
        INFO(c.name);
        const std::string n = c.name;
        const auto t = golden["t/" + n], f0Raw = golden["f0_raw/" + n], conf = golden["conf/" + n], f0 = golden["f0/" + n];
        const auto trk = detect(sourceBand(c.input, kFs, 30.0, 120.0), kFs, 30.0);
        REQUIRE(trk.samples.size() == t.size());
        int mismatches = 0;
        int estimates = 0;
        for (size_t i = 0; i < t.size(); ++i)
        {
            mismatches += trk.samples[i] == t[i] ? 0 : 1;
            mismatches += sameOrBothNaN(trk.f0Raw[i], f0Raw[i], 1e-6) ? 0 : 1;
            mismatches += std::abs(trk.conf[i] - conf[i]) <= 1e-6 ? 0 : 1;
            mismatches += sameOrBothNaN(trk.f0[i], f0[i], 1e-6) ? 0 : 1;
            estimates += std::isnan(f0[i]) ? 0 : 1;
        }
        CHECK(mismatches == 0);
        CHECK(estimates > static_cast<int>(t.size()) / 2);
    }
}
