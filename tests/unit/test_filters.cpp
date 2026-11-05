// SPDX-License-Identifier: AGPL-3.0-or-later
// TPT/Cytomic SVF sections, Butterworth/Linkwitz-Riley/Bessel designs and cascades (T-FILT-01..04, K-01).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "dsp/Svf.h"
#include "support/AllocGuard.h"
#include "support/Golden.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::dsp;
namespace sig = subgen::test::signals;

namespace
{
constexpr std::array<double, 4> kFs{44100.0, 48000.0, 96000.0, 192000.0};
constexpr std::array<double, 6> kFc{15.0, 22.0, 30.0, 60.0, 120.0, 500.0};

double magDb(std::complex<double> h)
{
    return 20.0 * std::log10(std::abs(h));
}

std::string key(const char* family, int order, FilterKind kind, double fs)
{
    return std::string(family) + std::to_string(order) + (kind == FilterKind::LowPass ? "/LP/" : "/HP/")
         + std::to_string(static_cast<int>(fs));
}

// Worst |dB| error and worst relative group-delay error at fc of our designs against SciPy (tests/golden/filters.sgg).
struct Deviation
{
    double magDb = 0.0;
    double groupDelayAtFc = 0.0;
};

Deviation compareWithScipy(FilterFamily family, const char* tag, int order)
{
    const auto golden = test::Golden::load("filters.sgg");
    Deviation worst;
    for (const double fs : kFs)
    {
        const auto freqs = golden["freqs/" + std::to_string(static_cast<int>(fs))];
        for (const auto kind : {FilterKind::LowPass, FilterKind::HighPass})
        {
            const auto mags = golden["mag_db/" + key(tag, order, kind, fs)];
            const auto delays = golden["gd_fc_s/" + key(tag, order, kind, fs)];
            for (size_t c = 0; c < kFc.size(); ++c)
            {
                FilterCascade cascade;
                cascade.set(designFilter(family, kind, order, kFc[c], fs));
                for (size_t i = 0; i < freqs.size(); ++i)
                    worst.magDb = std::max(worst.magDb, std::abs(magDb(cascade.response(freqs[i], fs))
                                                                 - mags[c * freqs.size() + i]));
                const double gd = cascade.groupDelaySeconds(kFc[c], fs);
                worst.groupDelayAtFc = std::max(worst.groupDelayAtFc, std::abs(gd / delays[c] - 1.0));
            }
        }
    }
    return worst;
}

// RBJ Audio EQ Cookbook biquads (Q form), evaluated on the unit circle.
std::complex<double> rbj(SvfMode mode, double f0, double q, double gainDb, double fs, double f)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * f0 / fs;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double s = 2.0 * std::sqrt(a) * alpha;
    std::array<double, 3> b{}, d{};
    if (mode == SvfMode::Bell)
    {
        b = {1.0 + alpha * a, -2.0 * cw, 1.0 - alpha * a};
        d = {1.0 + alpha / a, -2.0 * cw, 1.0 - alpha / a};
    }
    else if (mode == SvfMode::LowShelf)
    {
        b = {a * ((a + 1.0) - (a - 1.0) * cw + s), 2.0 * a * ((a - 1.0) - (a + 1.0) * cw),
             a * ((a + 1.0) - (a - 1.0) * cw - s)};
        d = {(a + 1.0) + (a - 1.0) * cw + s, -2.0 * ((a - 1.0) + (a + 1.0) * cw), (a + 1.0) + (a - 1.0) * cw - s};
    }
    else
    {
        b = {a * ((a + 1.0) + (a - 1.0) * cw + s), -2.0 * a * ((a - 1.0) + (a + 1.0) * cw),
             a * ((a + 1.0) + (a - 1.0) * cw - s)};
        d = {(a + 1.0) - (a - 1.0) * cw + s, 2.0 * ((a - 1.0) - (a + 1.0) * cw), (a + 1.0) - (a - 1.0) * cw - s};
    }
    const auto z1 = std::polar(1.0, -2.0 * std::numbers::pi * f / fs);
    const auto z2 = z1 * z1;
    return (b[0] + b[1] * z1 + b[2] * z2) / (d[0] + d[1] * z1 + d[2] * z2);
}
} // namespace

TEST_CASE("T-FILT-01 Butterworth and LR match SciPy within 0.01 dB from 5 Hz to 0.45 fs", "[filters]")
{
    for (const int order : {2, 4, 8, 16})
    {
        const auto dev = compareWithScipy(FilterFamily::Butterworth, "BW", order);
        INFO("Butterworth order " << order);
        CHECK(dev.magDb <= 0.01);
        CHECK(dev.groupDelayAtFc <= 0.005); // group delay at fc within 0.5 %
    }
    for (const int order : {4, 8, 16})
    {
        const auto dev = compareWithScipy(FilterFamily::LinkwitzRiley, "LR", order);
        INFO("Linkwitz-Riley order " << order);
        CHECK(dev.magDb <= 0.01);
        CHECK(dev.groupDelayAtFc <= 0.005);
    }
}

TEST_CASE("T-FILT-04 Bessel matches SciPy within 0.01 dB; group delay is flat to fc/2", "[filters]")
{
    for (const int order : {2, 4, 8})
    {
        const auto dev = compareWithScipy(FilterFamily::Bessel, "BESSEL", order);
        INFO("Bessel order " << order);
        CHECK(dev.magDb <= 0.01);
        CHECK(dev.groupDelayAtFc <= 0.005);

        FilterCascade lp;
        lp.set(designFilter(FilterFamily::Bessel, FilterKind::LowPass, order, 120.0, 48000.0));
        const double ref = lp.groupDelaySeconds(1.2, 48000.0);
        double variation = 0.0;
        for (double f = 1.2; f <= 60.0; f *= 1.05)
            variation = std::max(variation, std::abs(lp.groupDelaySeconds(f, 48000.0) / ref - 1.0));
        CHECK(variation <= (order == 2 ? 0.025 : 0.01));
    }
}

TEST_CASE("K-01 Butterworth order 3 has Q = 1 and a first-order section", "[filters]")
{
    const auto d = designFilter(FilterFamily::Butterworth, FilterKind::LowPass, 3, 100.0, 48000.0);
    REQUIRE(d.numSos == 1);
    CHECK(std::abs(d.sos[0].k - 1.0) <= 1e-12); // k = 1/Q
    CHECK(d.hasFirstOrder);
    CHECK(!d.first.highPass);
}

TEST_CASE("LR2 (12 dB/oct) is a first-order Butterworth squared", "[filters]")
{
    for (const auto kind : {FilterKind::LowPass, FilterKind::HighPass})
    {
        FilterCascade lr2, bw1;
        lr2.set(designFilter(FilterFamily::LinkwitzRiley, kind, 2, 80.0, 48000.0));
        bw1.set(designFilter(FilterFamily::Butterworth, kind, 1, 80.0, 48000.0));
        for (double f = 5.0; f < 20000.0; f *= 1.3)
        {
            const auto h = bw1.response(f, 48000.0);
            CHECK(std::abs(lr2.response(f, 48000.0) - h * h) <= 1e-12 * std::max(1.0, std::abs(h * h)));
        }
    }
    CHECK(slopeToOrder(12) == 2);
    CHECK(slopeToOrder(24) == 4);
    CHECK(slopeToOrder(48) == 8);
    CHECK(slopeToOrder(96) == 16);
}

TEST_CASE("T-FILT-02 Cytomic bell and shelves match RBJ within 0.05 dB (10 Hz-20 kHz, -18..18 dB)", "[filters]")
{
    double worst = 0.0;
    for (const double fs : {44100.0, 48000.0})
        for (const auto mode : {SvfMode::Bell, SvfMode::LowShelf, SvfMode::HighShelf})
            for (const double f0 : {30.0, 100.0, 1000.0, 8000.0})
                for (const double q : {0.3, 0.71, 2.0, 10.0})
                    for (double gain = -18.0; gain <= 18.0; gain += 6.0)
                    {
                        SvfSection s;
                        s.set(makeSvf(mode, f0, q, gain, fs));
                        for (double f = 10.0; f <= 20000.0; f *= 1.1)
                            worst = std::max(worst, std::abs(magDb(s.response(f, fs)) - magDb(rbj(mode, f0, q, gain, fs, f))));
                    }
    CHECK(worst <= 0.05);
}

TEST_CASE("T-FILT-02 0 dB shapes are the identity within 1e-12", "[filters]")
{
    const auto x = sig::xorshiftNoise(4800, 9);
    for (const auto mode : {SvfMode::Bell, SvfMode::LowShelf, SvfMode::HighShelf})
    {
        SvfSection s;
        s.set(makeSvf(mode, 60.0, 0.71, 0.0, 48000.0));
        double worst = 0.0;
        for (const double v : x)
            worst = std::max(worst, std::abs(s.process(v) - v));
        CHECK(worst <= 1e-12);
    }
}

TEST_CASE("T-FILT-03 cutoff modulated 15-500 Hz stays finite with |y| <= 4 max|x|", "[filters]")
{
    constexpr double fs = 48000.0;
    const auto x = sig::bandNoise(fs, 2.0, 20.0, 2000.0, -6.0, 3);
    const auto random = sig::xorshiftNoise(x.size(), 21);
    double xmax = 0.0;
    for (const double v : x)
        xmax = std::max(xmax, std::abs(v));
    for (const auto mode : {SvfMode::LowPass, SvfMode::HighPass, SvfMode::BandPass})
        for (const double q : {0.5, 0.71, 4.0})
            for (const bool sineModulation : {true, false})
            {
                SvfSection s;
                double ymax = 0.0;
                bool finite = true;
                for (size_t i = 0; i < x.size(); ++i)
                {
                    const double m = sineModulation ? std::sin(2.0 * std::numbers::pi * 10.0 * static_cast<double>(i) / fs)
                                                    : random[i];
                    const double fc = 15.0 * std::pow(500.0 / 15.0, 0.5 * (m + 1.0)); // 15..500 Hz, log
                    s.set(makeSvf(mode, fc, q, 0.0, fs));
                    const double y = s.process(x[i]);
                    finite = finite && std::isfinite(y);
                    ymax = std::max(ymax, std::abs(y));
                }
                INFO("mode " << static_cast<int>(mode) << " q " << q << " sine " << sineModulation);
                CHECK(finite);
                CHECK(ymax <= 4.0 * xmax);
            }
}

TEST_CASE("processed sine gain equals response() within 0.001 dB", "[filters]")
{
    constexpr double fs = 48000.0;
    struct Case
    {
        FilterFamily family;
        FilterKind kind;
        int order;
        double fc;
        double f;
    };
    for (const auto& c : {Case{FilterFamily::Butterworth, FilterKind::LowPass, 4, 120.0, 90.0},
                          Case{FilterFamily::Butterworth, FilterKind::HighPass, 3, 30.0, 40.0},
                          Case{FilterFamily::LinkwitzRiley, FilterKind::LowPass, 8, 80.0, 60.0},
                          Case{FilterFamily::Bessel, FilterKind::HighPass, 8, 22.0, 50.0}})
    {
        FilterCascade cascade;
        cascade.set(designFilter(c.family, c.kind, c.order, c.fc, fs));
        auto x = sig::sine(c.f, fs, 3.0, -6.0);
        cascade.process(x);
        const std::span<const double> tail(x.data() + 48000, 96000); // the last 2 s: whole cycles, settled
        const double measured = 20.0 * std::log10(test::rms(tail) / (std::pow(10.0, -6.0 / 20.0) / std::sqrt(2.0)));
        CHECK(std::abs(measured - magDb(cascade.response(c.f, fs))) <= 0.001);
    }
}

TEST_CASE("designFilter and process do not allocate", "[filters]")
{
    FilterCascade cascade;
    SvfSection section;
    double sink = 0.0;
    size_t allocations = 0;
    {
        test::AllocGuard guard;
        for (const int order : {1, 2, 3, 8, 16})
            cascade.set(designFilter(FilterFamily::Butterworth, FilterKind::HighPass, order, 30.0, 48000.0));
        cascade.set(designFilter(FilterFamily::Bessel, FilterKind::LowPass, 8, 120.0, 48000.0));
        section.set(makeSvf(SvfMode::Bell, 60.0, 1.0, 6.0, 48000.0));
        for (int i = 0; i < 1000; ++i)
            sink += cascade.process(section.process(i % 7 == 0 ? 1.0 : 0.0));
        allocations = guard.count();
    }
    CHECK(allocations == 0);
    CHECK(std::isfinite(sink));
}
