// SPDX-License-Identifier: AGPL-3.0-or-later
// Linear Phase low-pass (SPEC §7.6, T-FILT-06, SPK-6): the kernel design and its error, the uniformly partitioned
// convolution, the background design swapped in with a crossfade, the constant latency L_lin = (N_LIN - 1)/2 + 256.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "chain/LinearPhaseLp.h"
#include "chain/SubFilters.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "dsp/Fft.h"
#include "dsp/FirDesign.h"
#include "dsp/PartitionedConvolver.h"
#include "support/AllocGuard.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::chain;
using dsp::FilterFamily;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kWaitMs = 20000;

// |H| of a minimum-phase low-pass prototype, floored at 1e-15 (the prototype's lp_only)
struct LowPassMag
{
    dsp::FilterCascade cascade;
    double fs;

    LowPassMag(FilterFamily family, int order, double fc, double sampleRate) : fs(sampleRate)
    {
        cascade.set(dsp::designFilter(family, dsp::FilterKind::LowPass, order, fc, fs));
    }
    double operator()(double f) const { return std::max(std::abs(cascade.response(f, fs)), 1e-15); }
};

std::vector<double> kernelFor(const LowPassMag& mag, double fs)
{
    std::vector<double> h(static_cast<size_t>(dsp::linearPhaseTaps(fs)));
    dsp::designLinearPhaseKernel([&](double f) { return mag(f); }, fs, h);
    return h;
}

// The amplitude error of an impulse response against mag, measured apart from dsp::kernelErrorDb: a 2^19-point FFT,
// every bin from 5 Hz to the first one at or below -60 dB.
double measuredErrorDb(std::span<const double> h, const LowPassMag& mag, double fs)
{
    constexpr int kOrder = 19;
    const size_t n = size_t{1} << kOrder;
    std::vector<double> padded(n, 0.0);
    std::copy(h.begin(), h.end(), padded.begin());
    std::vector<std::complex<double>> spectrum(n / 2 + 1);
    dsp::RealFft fft(kOrder);
    fft.forward(padded.data(), spectrum.data());
    double worst = 0.0;
    for (size_t k = static_cast<size_t>(std::ceil(5.0 * static_cast<double>(n) / fs)); k <= n / 2; ++k)
    {
        const double f = static_cast<double>(k) * fs / static_cast<double>(n);
        const double target = mag(f);
        worst = std::max(worst, std::abs(20.0 * std::log10(std::abs(spectrum[k]) / target)));
        if (20.0 * std::log10(target) <= -60.0)
            break;
    }
    return worst;
}

SubFilterParams lowPass(FilterFamily family, int slope, double fc)
{
    SubFilterParams p;
    p.family = family;
    p.lpSlope = slope;
    p.lpHz = fc;
    p.linearPhase = true;
    return p;
}

template <typename Stage>
std::vector<double> runBlocks(Stage& stage, std::vector<double> x, size_t block)
{
    for (size_t i = 0; i < x.size(); i += block)
        stage.process(std::span<double>(x.data() + i, std::min(block, x.size() - i)));
    return x;
}

std::vector<double> impulse(size_t n)
{
    std::vector<double> x(n, 0.0);
    x[0] = 1.0;
    return x;
}

size_t peakIndex(const std::vector<double>& y)
{
    return static_cast<size_t>(std::max_element(y.begin(), y.end(), [](double a, double b) { return std::abs(a) < std::abs(b); }) - y.begin());
}

double maxSecondDifference(const std::vector<double>& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t>(from, 2); i < to; ++i)
        worst = std::max(worst, std::abs(y[i] - 2.0 * y[i - 1] + y[i - 2]));
    return worst;
}

// The amplitude of the tone at f in y[from, to): a least-squares fit of a cos + b sin.
double toneAmplitude(const std::vector<double>& y, double f, size_t from, size_t to)
{
    double cc = 0.0, ss = 0.0, cs = 0.0, yc = 0.0, ys = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * std::numbers::pi * f * static_cast<double>(i) / kFs;
        const double c = std::cos(w), s = std::sin(w);
        cc += c * c;
        ss += s * s;
        cs += c * s;
        yc += y[i] * c;
        ys += y[i] * s;
    }
    const double det = cc * ss - cs * cs;
    return std::abs(std::complex<double>((yc * ss - ys * cs) / det, (ys * cc - yc * cs) / det));
}
} // namespace

TEST_CASE("T-FILT-06 kernel symmetric exactly", "[linear-phase]")
{
    CHECK(dsp::linearPhaseTaps(44100.0) == 4097);
    CHECK(dsp::linearPhaseTaps(48000.0) == 4097);
    CHECK(dsp::linearPhaseTaps(88200.0) == 8193);
    CHECK(dsp::linearPhaseTaps(96000.0) == 8193);
    CHECK(dsp::linearPhaseTaps(176400.0) == 16385);
    CHECK(dsp::linearPhaseTaps(192000.0) == 16385);
    for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        INFO("fs " << fs);
        const auto h = kernelFor(LowPassMag(FilterFamily::Butterworth, 4, 120.0, fs), fs);
        bool symmetric = true;
        for (size_t n = 0; n < h.size(); ++n)
            symmetric = symmetric && h[n] == h[h.size() - 1 - n];
        CHECK(symmetric);
        CHECK(peakIndex(h) == (h.size() - 1) / 2); // the delay (N_LIN - 1)/2
    }
}

TEST_CASE("error <= 0.1 dB from 5 Hz to the -60 dB point for BW24 120 Hz, BW12 50 Hz, Bessel24 60 Hz and LR24 90 Hz at 48 kHz",
          "[linear-phase]")
{
    struct Case
    {
        FilterFamily family;
        int order;
        double fc;
    };
    for (const auto& c : {Case{FilterFamily::Butterworth, 4, 120.0}, Case{FilterFamily::Butterworth, 2, 50.0},
                          Case{FilterFamily::Bessel, 4, 60.0}, Case{FilterFamily::LinkwitzRiley, 4, 90.0},
                          Case{FilterFamily::Butterworth, 4, 140.0}})
    {
        const LowPassMag mag(c.family, c.order, c.fc, kFs);
        const auto h = kernelFor(mag, kFs);
        const double error = dsp::kernelErrorDb(h, [&](double f) { return mag(f); }, kFs);
        INFO("family " << static_cast<int>(c.family) << " order " << c.order << " at " << c.fc << " Hz: " << error << " dB");
        CHECK(error <= 0.1);
        CHECK(std::abs(measuredErrorDb(h, mag, kFs) - error) <= 1e-9);
    }
    // the kernel follows |H|, not |H|^2: Butterworth 24 dB/oct is -3.01 dB at its corner, not -6
    const LowPassMag mag(FilterFamily::Butterworth, 4, 120.0, kFs);
    const auto h = kernelFor(mag, kFs);
    std::complex<double> at = 0.0;
    for (size_t n = 0; n < h.size(); ++n)
        at += h[n] * std::polar(1.0, -2.0 * std::numbers::pi * 120.0 * static_cast<double>(n) / kFs);
    CHECK(std::abs(20.0 * std::log10(std::abs(at)) + 3.0103) <= 0.05);
}

TEST_CASE("reported error equals the measured one within 0.01 dB (BW24 at 60 Hz, ~0.33 dB)", "[linear-phase]")
{
    LinearPhaseLp lp;
    lp.prepare(kFs, 512);
    lp.requestDesign(lowPass(FilterFamily::Butterworth, 24, 60.0));
    REQUIRE(lp.waitUntilDesigned(kWaitMs));
    runBlocks(lp, std::vector<double>(4800, 0.0), 128); // the first kernel fades in over 40 ms
    const auto y = runBlocks(lp, impulse(8192), 128);
    const std::vector<double> ir(y.begin() + 256, y.begin() + 256 + 4097); // the kernel, after the 256-sample partition
    const double measured = measuredErrorDb(ir, LowPassMag(FilterFamily::Butterworth, 4, 60.0, kFs), kFs);
    INFO("reported " << lp.errorDb() << " dB, measured " << measured << " dB");
    CHECK(std::abs(lp.errorDb() - measured) <= 0.01);
    CHECK(std::abs(lp.errorDb() - 0.33) <= 0.03); // P2-08: 0.33 dB
}

TEST_CASE("delay is exactly (N_LIN - 1)/2 + 256", "[linear-phase]")
{
    for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        INFO("fs " << fs);
        const int taps = dsp::linearPhaseTaps(fs);
        LinearPhaseLp lp;
        lp.prepare(fs, 512);
        CHECK(lp.latencySamples() == (taps - 1) / 2 + 256);
        lp.requestDesign(lowPass(FilterFamily::Butterworth, 24, 120.0));
        REQUIRE(lp.waitUntilDesigned(kWaitMs));
        runBlocks(lp, std::vector<double>(static_cast<size_t>(0.1 * fs), 0.0), 256);
        const auto y = runBlocks(lp, impulse(static_cast<size_t>(2 * taps)), 256);
        CHECK(peakIndex(y) == static_cast<size_t>(lp.latencySamples()));
        SubFilters f;
        f.prepare(fs, 512, Quality::Print);
        f.setParams(lowPass(FilterFamily::Butterworth, 24, 120.0));
        CHECK(f.latencySamples() == lp.latencySamples());
    }
}

TEST_CASE("kernel swap crossfades without clicks", "[linear-phase]")
{
    const auto x = sig::sine(55.0, kFs, 1.0, -6.0);
    LinearPhaseLp lp;
    lp.prepare(kFs, 512);
    lp.requestDesign(lowPass(FilterFamily::Butterworth, 24, 140.0));
    REQUIRE(lp.waitUntilDesigned(kWaitMs));
    std::vector<double> y(x);
    size_t allocations = 0;
    for (size_t i = 0; i < y.size(); i += 128)
    {
        if (i == 24064)
        {
            {
                test::AllocGuard guard;
                lp.requestDesign(lowPass(FilterFamily::Butterworth, 24, 70.0));
                allocations += guard.count();
            }
            REQUIRE(lp.waitUntilDesigned(kWaitMs));
        }
        test::AllocGuard guard;
        lp.process(std::span<double>(y.data() + i, std::min<size_t>(128, y.size() - i)));
        allocations += guard.count();
    }
    CHECK(allocations == 0);
    const double steady = maxSecondDifference(y, 12000, 24000);
    INFO("steady " << steady << ", swap " << maxSecondDifference(y, 24000, 36000));
    CHECK(maxSecondDifference(y, 24000, 36000) <= 1.5 * steady + 1e-4);
    // the new kernel sounds after its 40 ms crossfade: the gain at 55 Hz is the 70 Hz kernel's own
    const auto k70 = kernelFor(LowPassMag(FilterFamily::Butterworth, 4, 70.0, kFs), kFs);
    std::complex<double> at = 0.0;
    for (size_t n = 0; n < k70.size(); ++n)
        at += k70[n] * std::polar(1.0, -2.0 * std::numbers::pi * 55.0 * static_cast<double>(n) / kFs);
    const double gain = toneAmplitude(y, 55.0, 36000, 48000) / toneAmplitude(x, 55.0, 36000, 48000);
    CHECK(std::abs(20.0 * std::log10(gain / std::abs(at))) <= 0.01);
}

TEST_CASE("latency does not change with sub_lp_freq or slope", "[linear-phase]")
{
    constexpr int kLatency = (4097 - 1) / 2 + 256;
    SubFilters f;
    f.prepare(kFs, 512, Quality::Print);
    for (const FilterFamily family : {FilterFamily::Butterworth, FilterFamily::LinkwitzRiley, FilterFamily::Bessel})
        for (const int slope : {12, 24, 48, 96})
            for (const double fc : {30.0, 140.0, 500.0})
            {
                f.setParams(lowPass(family, slope, fc));
                CHECK(f.latencySamples() == kLatency);
            }
    auto lfe = lowPass(FilterFamily::Butterworth, 24, 140.0);
    lfe.lfeMode = true;
    f.setParams(lfe);
    CHECK(f.latencySamples() == kLatency);
    // Min Phase, or Linear Phase outside Print: no latency
    auto minPhase = lowPass(FilterFamily::Butterworth, 24, 140.0);
    minPhase.linearPhase = false;
    f.setParams(minPhase);
    CHECK(f.latencySamples() == 0);
    SubFilters mix;
    mix.prepare(kFs, 512, Quality::Mix);
    mix.setParams(lowPass(FilterFamily::Butterworth, 24, 140.0));
    CHECK(mix.latencySamples() == 0);
}

TEST_CASE("convolver matches direct convolution within 1e-12", "[linear-phase]")
{
    const auto x = sig::xorshiftNoise(6000, 3);
    for (const size_t taps : {size_t{1}, size_t{100}, size_t{256}, size_t{1000}, size_t{4097}})
        for (const size_t block : {size_t{1}, size_t{37}, size_t{256}, size_t{300}})
        {
            INFO(taps << " taps, blocks of " << block);
            auto h = sig::xorshiftNoise(taps, 11);
            for (auto& v : h)
                v /= std::sqrt(static_cast<double>(taps));
            dsp::PartitionedConvolver c;
            c.prepare(4097, 256);
            c.setKernel(h);
            CHECK(c.latencySamples() == 256);
            const auto y = runBlocks(c, x, block);
            double worst = 0.0;
            for (size_t t = 0; t < y.size(); ++t)
            {
                double direct = 0.0;
                for (size_t m = 0; m < taps && m + 256 <= t; ++m)
                    direct += h[m] * x[t - 256 - m];
                worst = std::max(worst, std::abs(y[t] - direct));
            }
            CHECK(worst <= 1e-12);
        }
}

TEST_CASE("Linear Phase in Print: the min-phase high-pass, then the kernel", "[linear-phase]")
{
    for (const bool lfeMode : {false, true})
    {
        INFO("lfe " << lfeMode);
        auto p = lowPass(FilterFamily::Butterworth, 24, 120.0);
        p.lfeMode = lfeMode;
        p.lfeLpHz = 100.0;
        p.lfeLpSlope = 48;
        SubFilters f;
        f.prepare(kFs, 512, Quality::Print);
        f.setParams(p);
        REQUIRE(f.waitForLinearPhase(kWaitMs));
        runBlocks(f, std::vector<double>(4800, 0.0), 128);
        const auto y = runBlocks(f, impulse(16384), 128);
        // the reference: the high-pass cascade, then the kernel of the low-pass, after the 256-sample partition
        dsp::FilterCascade hp;
        hp.set(dsp::designFilter(FilterFamily::Butterworth, dsp::FilterKind::HighPass, 4, lfeMode ? 20.0 : 22.0, kFs));
        auto hpIr = impulse(16384);
        for (auto& v : hpIr)
            v = hp.process(v);
        const auto kernel = kernelFor(lfeMode ? LowPassMag(FilterFamily::Butterworth, 8, 100.0, kFs)
                                              : LowPassMag(FilterFamily::Butterworth, 4, 120.0, kFs),
                                      kFs);
        double worst = 0.0;
        for (size_t t = 256; t < y.size(); ++t)
        {
            double expected = 0.0;
            for (size_t m = 0; m < kernel.size() && m + 256 <= t; ++m)
                expected += kernel[m] * hpIr[t - 256 - m];
            worst = std::max(worst, std::abs(y[t] - expected));
        }
        CHECK(worst <= 1e-9);
        // the kernel's delay is latency (compensated with the dry path): the displayed group delay is the high-pass's
        CHECK(std::abs(f.groupDelayMs(40.0) - 1000.0 * hp.groupDelaySeconds(40.0, kFs)) <= 1e-9);
    }
}

TEST_CASE("switching to Linear Phase and back crossfades without clicks", "[linear-phase]")
{
    const auto x = sig::sine(55.0, kFs, 1.5, -6.0);
    SubFilters f;
    f.prepare(kFs, 512, Quality::Print);
    auto p = lowPass(FilterFamily::Butterworth, 24, 140.0);
    p.linearPhase = false;
    f.setParams(p);
    std::vector<double> y(x);
    size_t allocations = 0;
    for (size_t i = 0; i < y.size(); i += 128)
    {
        if (i == 24064 || i == 48128)
        {
            p.linearPhase = i == 24064;
            f.setParams(p);
            if (p.linearPhase)
                REQUIRE(f.waitForLinearPhase(kWaitMs));
        }
        test::AllocGuard guard;
        f.process(std::span<double>(y.data() + i, std::min<size_t>(128, y.size() - i)));
        allocations += guard.count();
    }
    CHECK(allocations == 0);
    const double steady = maxSecondDifference(y, 12000, 24000);
    INFO("steady " << steady << ", to linear " << maxSecondDifference(y, 24000, 36000) << ", back "
                   << maxSecondDifference(y, 48000, 60000));
    CHECK(maxSecondDifference(y, 24000, 36000) <= 1.5 * steady + 1e-4);
    CHECK(maxSecondDifference(y, 48000, 60000) <= 1.5 * steady + 1e-4);
}

TEST_CASE("offline Print renders wait for each kernel and are deterministic", "[linear-phase]")
{
    // an offline bounce runs much faster than the worker designs: it waits for every kernel (no deadline offline),
    // which a real-time render does not
    const auto x = sig::bass808(kFs);
    auto render = [&](bool offline, bool waitByHand) {
        SubFilters f;
        f.prepare(kFs, 512, Quality::Print);
        f.setNonRealtime(offline);
        auto p = lowPass(FilterFamily::Butterworth, 24, 120.0);
        f.setParams(p);
        if (waitByHand)
            REQUIRE(f.waitForLinearPhase(kWaitMs));
        std::vector<double> y(x);
        for (size_t i = 0; i < y.size(); i += 128)
        {
            if (i == 9600)
            {
                p.lpHz = 80.0;
                f.setParams(p);
                if (waitByHand)
                    REQUIRE(f.waitForLinearPhase(kWaitMs));
            }
            f.process(std::span<double>(y.data() + i, std::min<size_t>(128, y.size() - i)));
        }
        return y;
    };
    const auto offline = render(true, false);
    CHECK(offline == render(false, true)); // as a real-time render that waited by hand
    CHECK(offline == render(true, false));
}
