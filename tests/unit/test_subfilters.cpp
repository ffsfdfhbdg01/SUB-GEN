// SPDX-License-Identifier: AGPL-3.0-or-later
// The sub path's high- and low-pass (SPEC §7.6) and the LFE filters (SPEC_IO_UI §4; T-LFE-01, T-LFE-07): the cascade
// designs, LFE mode without a cascade with sub_lp, the displayed group delay, crossfaded structure changes.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "chain/SubFilters.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "support/AllocGuard.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::chain;
using dsp::FilterFamily;
using dsp::FilterKind;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

std::vector<double> run(SubFilters& f, std::vector<double> x, size_t block = 64)
{
    for (size_t i = 0; i < x.size(); i += block)
        f.process(std::span<double>(x.data() + i, std::min(block, x.size() - i)));
    return x;
}

struct Reference
{
    dsp::FilterCascade hp, lp;

    Reference(FilterFamily family, int hpOrder, double hpHz, int lpOrder, double lpHz, double fs = kFs)
    {
        hp.set(dsp::designFilter(family, FilterKind::HighPass, hpOrder, hpHz, fs));
        lp.set(dsp::designFilter(family, FilterKind::LowPass, lpOrder, lpHz, fs));
    }

    std::vector<double> run(std::vector<double> x)
    {
        for (auto& v : x)
            v = lp.process(hp.process(v));
        return x;
    }

    double attenuationDb(double f, double fs = kFs) const
    {
        return -20.0 * std::log10(std::abs(hp.response(f, fs) * lp.response(f, fs)));
    }
};

std::vector<double> impulse(size_t n)
{
    std::vector<double> x(n, 0.0);
    x[0] = 1.0;
    return x;
}

double maxAbsDiff(const std::vector<double>& a, const std::vector<double>& b)
{
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

SubFilterParams normal(FilterFamily family, double hpHz, int hpSlope, double lpHz, int lpSlope)
{
    SubFilterParams p;
    p.family = family;
    p.hpHz = hpHz;
    p.hpSlope = hpSlope;
    p.lpHz = lpHz;
    p.lpSlope = lpSlope;
    return p;
}

// Group delay of an impulse response at f (s): Re(sum n h[n] e^(-jwn) / sum h[n] e^(-jwn)) / fs.
double measuredGroupDelay(const std::vector<double>& h, double f)
{
    std::complex<double> num = 0.0, den = 0.0;
    for (size_t n = 0; n < h.size(); ++n)
    {
        const auto e = std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n) / kFs);
        num += static_cast<double>(n) * h[n] * e;
        den += h[n] * e;
    }
    return (num / den).real() / kFs;
}

double maxSecondDifference(const std::vector<double>& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t>(from, 2); i < to; ++i)
        worst = std::max(worst, std::abs(y[i] - 2.0 * y[i - 1] + y[i - 2]));
    return worst;
}

std::vector<double> slice(const std::vector<double>& y, size_t from, size_t to)
{
    return {y.begin() + static_cast<std::ptrdiff_t>(from), y.begin() + static_cast<std::ptrdiff_t>(to)};
}
} // namespace

TEST_CASE("responses equal the cascade designs", "[subfilters]")
{
    for (const FilterFamily family : {FilterFamily::Butterworth, FilterFamily::LinkwitzRiley, FilterFamily::Bessel})
        for (const int slope : {12, 24, 48, 96})
            for (const auto& [hp, lp] : {std::pair{22.0, 140.0}, std::pair{10.0, 500.0}, std::pair{60.0, 30.0}})
            {
                INFO("family " << static_cast<int>(family) << " slope " << slope << " hp " << hp << " lp " << lp);
                SubFilters f;
                f.prepare(kFs, 512, Quality::Mix);
                f.setParams(normal(family, hp, slope, lp, slope));
                const int order = family == FilterFamily::Bessel ? std::min(slope / 6, 8) : slope / 6; // Bessel <= 48
                const auto x = impulse(16384);
                CHECK(maxAbsDiff(run(f, x), Reference(family, order, hp, order, lp).run(x)) <= 1e-12);
                CHECK(f.latencySamples() == 0);
            }
    // different slopes for the two filters, at 96 kHz and in Live and Print alike
    for (const Quality q : {Quality::Live, Quality::Print})
    {
        SubFilters f;
        f.prepare(96000.0, 512, q);
        f.setParams(normal(FilterFamily::Butterworth, 30.0, 12, 200.0, 48));
        const auto x = impulse(32768);
        CHECK(maxAbsDiff(run(f, x), Reference(FilterFamily::Butterworth, 2, 30.0, 8, 200.0, 96000.0).run(x)) <= 1e-12);
        CHECK(f.latencySamples() == 0);
    }
}

TEST_CASE("T-LFE-01 white noise: >= 14 dB at 1.5 fc for 24 dB/oct and attenuation below lfe_hp", "[subfilters]")
{
    // periodic white noise: the second period of the output is the steady state, so its spectrum is H times the
    // noise's exactly (no window leakage from the deep stop band)
    constexpr size_t kN = size_t{1} << 20; // bins of 0.046 Hz: five in the narrowest band
    const auto noise = sig::xorshiftNoise(kN, 7);
    std::vector<double> twice(noise);
    twice.insert(twice.end(), noise.begin(), noise.end());
    for (const int slope : {12, 24, 48, 96})
    {
        INFO("lfe_lp_slope " << slope);
        SubFilters f;
        f.prepare(kFs, 512, Quality::Mix);
        SubFilterParams p;
        p.lfeMode = true;
        p.lfeHpHz = 20.0;
        p.lfeLpHz = 120.0;
        p.lfeLpSlope = slope;
        p.lpHz = 40.0; // sub_lp does not apply in LFE mode
        f.setParams(p);
        const auto y = slice(run(f, twice), kN, 2 * kN);
        const Reference design(FilterFamily::Butterworth, 4, 20.0, slope / 6, 120.0);
        auto attenuation = [&](double fc) {
            return test::bandDb(noise, kFs, 0.99 * fc, 1.01 * fc) - test::bandDb(y, kFs, 0.99 * fc, 1.01 * fc);
        };
        CHECK(std::abs(attenuation(180.0) - design.attenuationDb(180.0)) <= 0.5);
        CHECK(attenuation(180.0) >= 14.0 * slope / 24.0 - 1.0); // 12: 7.8, 24: 14.3, 48: 28.2, 96: 56.3 dB
        CHECK(attenuation(20.0 / 1.5) >= 14.0); // the LFE high-pass: 24 dB/oct
        CHECK(std::abs(attenuation(20.0 / 1.5) - design.attenuationDb(20.0 / 1.5)) <= 0.5);
        CHECK(std::abs(attenuation(60.0)) <= 0.5); // pass band
    }
}

TEST_CASE("T-LFE-07 displayed group delay at 40 Hz within 2 % of the measured one", "[subfilters]")
{
    SubFilterParams lfe;
    lfe.lfeMode = true; // the defaults: high-pass 20 Hz, low-pass Butterworth 24 dB/oct at 120 Hz
    SubFilterParams lfeSteep = lfe;
    lfeSteep.lfeLpSlope = 48;
    lfeSteep.family = FilterFamily::LinkwitzRiley;
    for (const auto& p : {lfe, lfeSteep, SubFilterParams{}, normal(FilterFamily::Bessel, 15.0, 48, 80.0, 48),
                          normal(FilterFamily::Butterworth, 30.0, 96, 300.0, 12)})
    {
        SubFilters f;
        f.prepare(kFs, 512, Quality::Mix);
        f.setParams(p);
        const double measured = 1000.0 * measuredGroupDelay(run(f, impulse(48000)), 40.0);
        INFO("lfe " << p.lfeMode << " family " << static_cast<int>(p.family) << ": displayed " << f.groupDelayMs(40.0)
                    << " ms, measured " << measured << " ms");
        CHECK(std::abs(f.groupDelayMs(40.0) / measured - 1.0) <= 0.02);
    }
}

TEST_CASE("LFE mode uses one low-pass, not a cascade with sub_lp", "[subfilters]")
{
    for (const FilterFamily family : {FilterFamily::Butterworth, FilterFamily::Bessel})
    {
        auto p = normal(family, 60.0, 48, 30.0, 96); // far from the LFE filters
        p.lfeMode = true;
        p.lfeHpHz = 25.0;
        p.lfeLpHz = 120.0;
        p.lfeLpSlope = 24;
        SubFilters f;
        f.prepare(kFs, 512, Quality::Mix);
        f.setParams(p);
        const auto x = impulse(16384);
        CHECK(maxAbsDiff(run(f, x), Reference(family, 4, 25.0, 4, 120.0).run(x)) <= 1e-12);
    }
}

TEST_CASE("family change crossfades without clicks", "[subfilters]")
{
    const auto x = sig::sine(55.0, kFs, 0.75, -6.0);
    SubFilters f;
    f.prepare(kFs, 512, Quality::Mix);
    SubFilterParams p;
    f.setParams(p);
    std::vector<double> y(x);
    for (size_t i = 0; i < y.size(); i += 64)
    {
        if (i == 12032)
            p.family = FilterFamily::Bessel;
        if (i == 18048)
        {
            p.hpSlope = 48;
            p.lpSlope = 12;
        }
        if (i == 24064)
            p.lfeMode = true;
        if (i == 30080)
            p.lfeLpHz = 60.0;
        if (i == 12032 || i == 18048 || i == 24064 || i == 30080)
            f.setParams(p);
        f.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
    }
    const double steady = maxSecondDifference(y, 6000, 12000);
    for (const size_t from : {12000, 18000, 24000, 30000})
    {
        INFO("steady " << steady << ", from " << from << ": " << maxSecondDifference(y, from, from + 6000));
        CHECK(maxSecondDifference(y, from, from + 6000) <= 1.5 * steady + 1e-4);
    }
}

TEST_CASE("the incoming filter is warmed up, and a change during a crossfade follows it", "[subfilters]")
{
    // once the 20 ms crossfade is over the output is the new design's: 20-40 ms after the change the residual is
    // -48 dB (a cold start leaves the incoming high-pass ringing: -29 dB)
    const auto x = sig::sine(55.0, kFs, 0.75, -6.0);
    auto changed = [&](std::initializer_list<std::pair<size_t, FilterFamily>> changes) {
        SubFilters f;
        f.prepare(kFs, 512, Quality::Mix);
        SubFilterParams p;
        f.setParams(p);
        std::vector<double> y(x);
        for (size_t i = 0; i < y.size(); i += 64)
        {
            for (const auto& [at, family] : changes)
                if (i == at)
                {
                    p.family = family;
                    f.setParams(p);
                }
            f.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
        }
        return y;
    };
    auto from = [&](FilterFamily family) {
        SubFilters f;
        f.prepare(kFs, 512, Quality::Mix);
        SubFilterParams p;
        p.family = family;
        f.setParams(p);
        return run(f, x);
    };
    const auto bessel = from(FilterFamily::Bessel);
    const auto y = changed({{12032, FilterFamily::Bessel}});
    INFO("null 20-40 ms after the change: " << test::nullDb(slice(bessel, 12992, 13952), slice(y, 12992, 13952)) << " dB");
    CHECK(test::nullDb(slice(bessel, 12992, 13952), slice(y, 12992, 13952)) <= -40.0);
    // Bessel asked 9 ms into the crossfade to Linkwitz-Riley: it follows that crossfade
    const auto lr = changed({{12032, FilterFamily::LinkwitzRiley}, {12480, FilterFamily::Bessel}});
    CHECK(test::nullDb(slice(bessel, 24000, 36000), slice(lr, 24000, 36000)) <= -30.0);
}

TEST_CASE("before setParams the filters pass the signal through", "[subfilters]")
{
    const auto x = sig::kick(kFs, {.nHits = 1, .spacing = 0.25, .levelDb = -3.0});
    SubFilters f;
    f.prepare(kFs, 512, Quality::Mix);
    CHECK(run(f, x) == x);
}

TEST_CASE("SubFilters do not allocate and do not depend on the block size", "[subfilters]")
{
    const auto x = sig::bass808(kFs);
    auto go = [&](size_t block) {
        SubFilters f;
        f.prepare(kFs, 512, Quality::Mix);
        SubFilterParams p;
        f.setParams(p);
        std::vector<double> y(x);
        size_t allocations = 0;
        for (size_t i = 0; i < y.size(); i += block)
        {
            if (i >= 9600 && i < 9600 + block) // parameters move once
            {
                p.lpHz = 90.0;
                p.family = FilterFamily::LinkwitzRiley;
                f.setParams(p);
            }
            const auto s = std::span<double>(y.data() + i, std::min(block, y.size() - i));
            test::AllocGuard guard;
            f.process(s);
            allocations += guard.count();
        }
        CHECK(allocations == 0);
        return y;
    };
    const auto a = go(64);
    CHECK(go(32) == a); // both divide 9600
    CHECK(go(320) == a);
}

TEST_CASE("subFiltersGroupDelayMs equals the filters' displayed group delay", "[subfilters]")
{
    SubFilterParams lfe;
    lfe.lfeMode = true;
    for (const auto& p : {SubFilterParams{}, lfe, normal(FilterFamily::Bessel, 15.0, 48, 80.0, 48),
                          normal(FilterFamily::LinkwitzRiley, 30.0, 96, 300.0, 12)})
        for (const double f : {30.0, 40.0, 80.0})
        {
            SubFilters filters;
            filters.prepare(kFs, 512, Quality::Mix);
            filters.setParams(p);
            CHECK(std::abs(subFiltersGroupDelayMs(p, false, kFs, f) - filters.groupDelayMs(f)) <= 1e-12);
        }
    // Linear Phase sounding: the kernel's delay is latency, the high-pass alone counts
    dsp::FilterCascade hp;
    hp.set(dsp::designFilter(FilterFamily::Butterworth, FilterKind::HighPass, 4, 22.0, kFs));
    CHECK(std::abs(subFiltersGroupDelayMs(SubFilterParams{}, true, kFs, 40.0) - 1000.0 * hp.groupDelaySeconds(40.0, kFs)) <= 1e-12);
}
