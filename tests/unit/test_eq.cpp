// SPDX-License-Identifier: AGPL-3.0-or-later
// Frequency Boost and the 5-band parametric EQ (SPEC §7.2, T-EQ-01): Cytomic bell and shelves, identity at 0 dB or
// off, the Boost centre following the Sub -1 frequency in log steps, type changes crossfaded.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

#include "chain/Boost.h"
#include "chain/Eq.h"
#include "support/AllocGuard.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::chain;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;

// |H| of the Cytomic shapes, written out from SPEC §7.2: A = 10^(gain/40), t = tan(pi f0/fs); H = (m0 D + m1 s + m2)/D,
// D = s^2 + k s + 1, s = j tan(pi f/fs) / g.
double cytomicDb(EqType type, double f0, double gainDb, double q, double f)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double t = std::tan(std::numbers::pi * f0 / kFs);
    double g = t, k = 1.0 / q, m0 = 1.0, m1 = 0.0, m2 = 0.0;
    switch (type)
    {
    case EqType::Bell:
        k = 1.0 / (q * a);
        m1 = k * (a * a - 1.0);
        break;
    case EqType::LowShelf:
        g = t / std::sqrt(a);
        m1 = k * (a - 1.0);
        m2 = a * a - 1.0;
        break;
    case EqType::HighShelf:
        g = t * std::sqrt(a);
        m0 = a * a;
        m1 = k * (1.0 - a) * a;
        m2 = 1.0 - a * a;
        break;
    }
    const std::complex<double> s(0.0, std::tan(std::numbers::pi * f / kFs) / g);
    const auto d = s * s + k * s + 1.0;
    return 20.0 * std::log10(std::abs((m0 * d + m1 * s + m2) / d));
}

// The complex amplitude of the tone at f in y[36000, 48000): a least-squares fit of a cos + b sin, exact for a steady
// sine whatever the window holds (a DFT over a non-integer number of half periods leaks the negative frequency).
std::complex<double> toneAt(const std::vector<double>& y, double f)
{
    double cc = 0.0, ss = 0.0, cs = 0.0, yc = 0.0, ys = 0.0;
    for (size_t i = 36000; i < 48000; ++i)
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
    return {(yc * ss - ys * cs) / det, (ys * cc - yc * cs) / det};
}

// The steady gain of a 1 s sine through a stage (dB), measured in its last 0.25 s.
template <typename Stage>
double measuredDb(Stage& stage, double f)
{
    const auto x = sig::sine(f, kFs, 1.0, -12.0);
    std::vector<double> y(x);
    stage.process(y);
    return 20.0 * std::log10(std::abs(toneAt(y, f)) / std::abs(toneAt(x, f)));
}

EqParams oneBand(EqType type, double f, double gainDb, double q)
{
    EqParams p;
    p.bands[0] = {true, type, f, gainDb, q};
    return p;
}

double maxSecondDifference(const std::vector<double>& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t>(from, 2); i < to; ++i)
        worst = std::max(worst, std::abs(y[i] - 2.0 * y[i - 1] + y[i - 2]));
    return worst;
}
} // namespace

TEST_CASE("T-EQ-01 band responses match the Cytomic formulas within 0.05 dB; off or 0 dB is bit-exact", "[eq]")
{
    for (const EqType type : {EqType::Bell, EqType::LowShelf, EqType::HighShelf})
        for (const double gain : {-12.0, 18.0})
            for (const double f0 : {40.0, 300.0})
                for (const double q : {0.7, 4.0})
                    for (const double f : {f0 / 2.0, f0, 2.0 * f0})
                    {
                        INFO("type " << static_cast<int>(type) << " gain " << gain << " f0 " << f0 << " q " << q
                                     << " at " << f << " Hz");
                        Eq eq;
                        eq.prepare(kFs);
                        eq.setParams(oneBand(type, f0, gain, q));
                        CHECK(std::abs(eq.curveDb(f) - cytomicDb(type, f0, gain, q, f)) <= 1e-9);
                        CHECK(std::abs(measuredDb(eq, f) - cytomicDb(type, f0, gain, q, f)) <= 0.05);
                    }
    // five bands in series add up in dB
    Eq eq;
    eq.prepare(kFs);
    EqParams p;
    p.bands = {EqBand{true, EqType::LowShelf, 40.0, 6.0, 0.71}, EqBand{true, EqType::Bell, 60.0, -4.0, 1.0},
               EqBand{true, EqType::Bell, 100.0, 3.0, 2.0}, EqBand{true, EqType::Bell, 160.0, -6.0, 1.0},
               EqBand{true, EqType::HighShelf, 300.0, -9.0, 0.71}};
    eq.setParams(p);
    for (const double f : {30.0, 55.0, 120.0, 400.0})
    {
        double sum = 0.0;
        for (const auto& b : p.bands)
            sum += cytomicDb(b.type, b.freqHz, b.gainDb, b.q, f);
        CHECK(std::abs(eq.curveDb(f) - sum) <= 1e-9);
        CHECK(std::abs(measuredDb(eq, f) - sum) <= 0.05);
    }
    // identity: all bands off, or on at 0 dB
    const auto x = sig::kick(kFs, {.nHits = 2, .spacing = 0.25, .levelDb = -3.0});
    for (const bool on : {false, true})
    {
        Eq id;
        id.prepare(kFs);
        EqParams q0; // the registry's defaults: off, 0 dB
        for (auto& b : q0.bands)
            b.on = on;
        id.setParams(q0);
        std::vector<double> y(x);
        id.process(y);
        CHECK(y == x);
    }
    // a type change or a band switched on at 0 dB stays bit-exact through the crossfades
    Eq id;
    id.prepare(kFs);
    EqParams q0;
    q0.bands[1].on = true;
    id.setParams(q0);
    std::vector<double> y(x);
    for (size_t i = 0; i < y.size(); i += 64)
    {
        if (i == 3008)
        {
            q0.bands[1].type = EqType::HighShelf;
            q0.bands[3].on = true;
            id.setParams(q0);
        }
        id.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
    }
    CHECK(y == x);
}

TEST_CASE("Boost follows f_sub1, log-smoothed over 20 ms", "[eq]")
{
    Boost boost;
    boost.prepare(kFs);
    BoostParams p;
    p.gainDb = 9.0;
    p.follow = true;
    p.fSub1Hz = 40.0;
    boost.setParams(p);
    std::vector<double> block(48, 0.0);
    for (int i = 0; i < 100; ++i)
        boost.process(block);
    CHECK(boost.centreHz() == 40.0);
    p.fSub1Hz = 80.0;
    boost.setParams(p);
    std::vector<double> half(480, 0.0); // 10 ms: half way in log, the geometric mean
    boost.process(half);
    CHECK(std::abs(boost.centreHz() / std::sqrt(40.0 * 80.0) - 1.0) <= 1e-3);
    boost.process(half);
    CHECK(std::abs(boost.centreHz() - 80.0) <= 1e-9);
    // HARMONIC has no sub pitch: the centre is boost_freq
    p.engineHasPitch = false;
    p.freqHz = 55.0;
    boost.setParams(p);
    std::vector<double> settle(4800, 0.0);
    boost.process(settle);
    CHECK(std::abs(boost.centreHz() - 55.0) <= 1e-9);
    p.follow = false;
    p.engineHasPitch = true;
    p.freqHz = 70.0;
    boost.setParams(p);
    boost.process(settle);
    CHECK(std::abs(boost.centreHz() - 70.0) <= 1e-9);
}

TEST_CASE("Boost 0 dB is bit-exact; its peak and shelf follow the Cytomic formulas", "[eq]")
{
    const auto x = sig::kick(kFs, {.nHits = 2, .spacing = 0.25, .levelDb = -3.0});
    for (const BoostType type : {BoostType::Peak, BoostType::LowShelf})
    {
        Boost boost;
        boost.prepare(kFs);
        BoostParams p;
        p.type = type;
        p.gainDb = 0.0;
        boost.setParams(p);
        std::vector<double> y(x);
        boost.process(y);
        CHECK(y == x);
        // a type change at 0 dB stays bit-exact through the crossfade
        std::vector<double> z(x);
        Boost changing;
        changing.prepare(kFs);
        changing.setParams(p);
        for (size_t i = 0; i < z.size(); i += 64)
        {
            if (i == 3008)
            {
                p.type = type == BoostType::Peak ? BoostType::LowShelf : BoostType::Peak;
                changing.setParams(p);
            }
            changing.process(std::span<double>(z.data() + i, std::min<size_t>(64, z.size() - i)));
        }
        CHECK(z == x);
    }
    for (const auto& [type, eqType] : {std::pair{BoostType::Peak, EqType::Bell}, std::pair{BoostType::LowShelf, EqType::LowShelf}})
        for (const double f : {30.0, 55.0, 110.0})
        {
            Boost boost;
            boost.prepare(kFs);
            BoostParams p;
            p.type = type;
            p.gainDb = 12.0;
            p.follow = false;
            p.freqHz = 55.0;
            p.q = 2.0;
            boost.setParams(p);
            CHECK(std::abs(measuredDb(boost, f) - cytomicDb(eqType, 55.0, 12.0, 2.0, f)) <= 0.05);
        }
}

TEST_CASE("type change crossfades without clicks", "[eq]")
{
    const auto x = sig::sine(55.0, kFs, 0.5, -6.0);
    {
        Boost boost;
        boost.prepare(kFs);
        BoostParams p;
        p.gainDb = 12.0;
        p.follow = false;
        p.freqHz = 50.0;
        boost.setParams(p);
        std::vector<double> y(x);
        for (size_t i = 0; i < y.size(); i += 64)
        {
            if (i == 12032)
            {
                p.type = BoostType::LowShelf;
                boost.setParams(p);
            }
            boost.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
        }
        const double steady = std::max(maxSecondDifference(y, 6000, 12000), maxSecondDifference(y, 18000, 24000));
        INFO("Boost: steady " << steady << ", change " << maxSecondDifference(y, 12000, 18000));
        CHECK(maxSecondDifference(y, 12000, 18000) <= 1.5 * steady + 1e-4);
    }
    {
        Eq eq;
        eq.prepare(kFs);
        auto p = oneBand(EqType::Bell, 60.0, 12.0, 1.0);
        eq.setParams(p);
        std::vector<double> y(x);
        for (size_t i = 0; i < y.size(); i += 64)
        {
            if (i == 12032)
            {
                p.bands[0].type = EqType::HighShelf;
                eq.setParams(p);
            }
            if (i == 18048)
            {
                p.bands[0].on = false;
                eq.setParams(p);
            }
            eq.process(std::span<double>(y.data() + i, std::min<size_t>(64, y.size() - i)));
        }
        const double steady = maxSecondDifference(y, 6000, 12000);
        INFO("EQ: steady " << steady << ", type change " << maxSecondDifference(y, 12000, 18000) << ", off "
                           << maxSecondDifference(y, 18000, 24000));
        CHECK(maxSecondDifference(y, 12000, 18000) <= 1.5 * steady + 1e-4);
        CHECK(maxSecondDifference(y, 18000, 24000) <= 1.5 * steady + 1e-4);
    }
}

TEST_CASE("Boost holds its centre while f_sub1 is not valid", "[eq]")
{
    Boost boost;
    boost.prepare(kFs);
    BoostParams p;
    p.gainDb = 9.0;
    p.freqHz = 55.0;
    p.fSub1Hz = 0.0; // no estimate yet: boost_freq
    boost.setParams(p);
    std::vector<double> settle(4800, 0.0);
    boost.process(settle);
    CHECK(boost.centreHz() == 55.0);
    p.fSub1Hz = 40.0;
    boost.setParams(p);
    boost.process(settle);
    CHECK(std::abs(boost.centreHz() - 40.0) <= 1e-9);
    for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()})
    {
        INFO("f_sub1 " << invalid);
        p.fSub1Hz = invalid;
        boost.setParams(p);
        boost.process(settle);
        CHECK(std::abs(boost.centreHz() - 40.0) <= 1e-9);
    }
}

TEST_CASE("a type change asked during a crossfade is applied after it", "[eq]")
{
    // Peak -> Low Shelf -> Peak 9 ms apart: the Boost ends on Peak; Bell -> High Shelf -> Low Shelf: the band ends on
    // Low Shelf
    {
        Boost boost;
        boost.prepare(kFs);
        BoostParams p;
        p.gainDb = 12.0;
        p.follow = false;
        p.q = 2.0;
        boost.setParams(p);
        std::vector<double> warm(9600, 0.0);
        boost.process(warm);
        p.type = BoostType::LowShelf;
        boost.setParams(p);
        std::vector<double> part(448, 0.0);
        boost.process(part);
        p.type = BoostType::Peak;
        boost.setParams(p);
        CHECK(std::abs(measuredDb(boost, 30.0) - cytomicDb(EqType::Bell, 55.0, 12.0, 2.0, 30.0)) <= 0.05);
    }
    {
        Eq eq;
        eq.prepare(kFs);
        auto p = oneBand(EqType::Bell, 60.0, 12.0, 1.0);
        eq.setParams(p);
        std::vector<double> warm(9600, 0.0);
        eq.process(warm);
        p.bands[0].type = EqType::HighShelf;
        eq.setParams(p);
        std::vector<double> part(448, 0.0);
        eq.process(part);
        p.bands[0].type = EqType::LowShelf;
        eq.setParams(p);
        CHECK(std::abs(measuredDb(eq, 30.0) - cytomicDb(EqType::LowShelf, 60.0, 12.0, 1.0, 30.0)) <= 0.05);
        CHECK(std::abs(eq.curveDb(30.0) - cytomicDb(EqType::LowShelf, 60.0, 12.0, 1.0, 30.0)) <= 1e-9);
    }
}

TEST_CASE("a band type outside the enum is a bell", "[eq]")
{
    for (const int stray : {3, 7, -1})
    {
        INFO("type " << stray);
        Eq eq;
        eq.prepare(kFs);
        eq.setParams(oneBand(static_cast<EqType>(stray), 60.0, 9.0, 1.0));
        CHECK(std::abs(eq.curveDb(40.0) - cytomicDb(EqType::Bell, 60.0, 9.0, 1.0, 40.0)) <= 1e-9);
        CHECK(std::abs(measuredDb(eq, 40.0) - cytomicDb(EqType::Bell, 60.0, 9.0, 1.0, 40.0)) <= 0.05);
    }
}

TEST_CASE("before setParams the Boost and the EQ pass the signal through", "[eq]")
{
    const auto x = sig::kick(kFs, {.nHits = 1, .spacing = 0.25, .levelDb = -3.0});
    Boost boost;
    boost.prepare(kFs);
    std::vector<double> y(x);
    boost.process(y);
    CHECK(y == x);
    Eq eq;
    eq.prepare(kFs);
    std::vector<double> z(x);
    eq.process(z);
    CHECK(z == x);
    // and they start cleanly when the parameters come
    BoostParams bp;
    bp.gainDb = 6.0;
    boost.setParams(bp);
    boost.process(y);
    eq.setParams(oneBand(EqType::Bell, 60.0, 6.0, 1.0));
    eq.process(z);
    CHECK(std::all_of(y.begin(), y.end(), [](double v) { return std::isfinite(v); }));
    CHECK(std::all_of(z.begin(), z.end(), [](double v) { return std::isfinite(v); }));
}

TEST_CASE("Boost and EQ do not allocate and do not depend on the block size", "[eq]")
{
    const auto x = sig::bass808(kFs);
    auto run = [&](size_t block) {
        Boost boost;
        Eq eq;
        boost.prepare(kFs);
        eq.prepare(kFs);
        BoostParams bp;
        bp.gainDb = 6.0;
        bp.fSub1Hz = 45.0;
        boost.setParams(bp);
        auto ep = oneBand(EqType::LowShelf, 40.0, 4.0, 0.71);
        ep.bands[2] = {true, EqType::Bell, 120.0, -5.0, 2.0};
        eq.setParams(ep);
        std::vector<double> y(x);
        size_t allocations = 0;
        for (size_t i = 0; i < y.size(); i += block)
        {
            if (i >= 9600 && i < 9600 + block) // parameters move once
            {
                bp.fSub1Hz = 60.0;
                boost.setParams(bp);
                ep.bands[2].freqHz = 90.0;
                eq.setParams(ep);
            }
            const auto s = std::span<double>(y.data() + i, std::min(block, y.size() - i));
            test::AllocGuard guard;
            boost.process(s);
            eq.process(s);
            allocations += guard.count();
        }
        CHECK(allocations == 0);
        return y;
    };
    const auto a = run(64);
    CHECK(run(32) == a); // both divide 9600
    CHECK(run(320) == a);
}
