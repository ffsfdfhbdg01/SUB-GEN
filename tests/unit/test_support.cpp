// SPDX-License-Identifier: AGPL-3.0-or-later
// Test support: FFT, measurements, test signals (against the Python reference), golden data, allocation guard.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "dsp/Fft.h"
#include "support/AllocGuard.h"
#include "support/Golden.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
namespace sig = subgen::test::signals;

namespace
{
double maxAbsDiff(std::span<const double> a, std::span<const double> b)
{
    REQUIRE(a.size() == b.size());
    double m = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}
} // namespace

TEST_CASE("fft roundtrip within 1e-12 (N = 8..65536)", "[support]")
{
    for (int order = 3; order <= 16; ++order)
    {
        dsp::RealFft fft(order);
        const int n = fft.size();
        REQUIRE(n == 1 << order);
        const auto x = sig::xorshiftNoise(static_cast<size_t>(n), 11);
        std::vector<std::complex<double>> spec(static_cast<size_t>(n / 2 + 1));
        std::vector<double> back(static_cast<size_t>(n));
        fft.forward(x.data(), spec.data());
        fft.inverse(spec.data(), back.data());
        CHECK(maxAbsDiff(x, back) <= 1e-12);
    }
}

TEST_CASE("fft matches a naive DFT for N = 64 within 1e-12", "[support]")
{
    constexpr int n = 64;
    dsp::RealFft fft(6);
    const auto x = sig::xorshiftNoise(n, 5);
    std::vector<std::complex<double>> spec(n / 2 + 1);
    fft.forward(x.data(), spec.data());
    for (int k = 0; k <= n / 2; ++k)
    {
        std::complex<double> ref{};
        for (int i = 0; i < n; ++i)
            ref += x[static_cast<size_t>(i)] * std::polar(1.0, -2.0 * std::numbers::pi * k * i / n);
        CHECK(std::abs(spec[static_cast<size_t>(k)] - ref) <= 1e-12);
    }
}

TEST_CASE("peakFreq finds 40.3 Hz within 0.01 Hz and amplitude within 1 %", "[support]")
{
    const auto x = sig::sine(40.3, 48000.0, 1.0, 20.0 * std::log10(0.5));
    const auto p = test::peakFreq(x, 48000.0, 20.0, 100.0);
    CHECK(std::abs(p.freqHz - 40.3) <= 0.01);
    CHECK(std::abs(p.amplitude / 0.5 - 1.0) <= 0.01);
}

TEST_CASE("thdDb of a sine with 1 % third harmonic is -40 +- 0.2 dB", "[support]")
{
    auto x = sig::sine(55.0, 48000.0, 1.0, -6.0);
    const auto h3 = sig::sine(165.0, 48000.0, 1.0, -46.0);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] += h3[i];
    CHECK(std::abs(test::thdDb(x, 48000.0, 55.0) + 40.0) <= 0.2);
}

TEST_CASE("bandDb of a sine with whole cycles equals its RMS level at a length that is not a power of two", "[support]")
{
    const auto x = sig::sine(1000.0, 48000.0, 1.0, -6.0); // 48000 samples, 1000 whole cycles
    CHECK(std::abs(test::bandDb(x, 48000.0, 900.0, 1100.0) - (-6.0 - 20.0 * std::log10(std::sqrt(2.0)))) <= 0.01);
    CHECK(test::bandDb(x, 48000.0, 1200.0, 24000.0) < -200.0);
    CHECK(std::abs(test::rms(x) - std::pow(10.0, -6.0 / 20.0) / std::sqrt(2.0)) <= 1e-12);
}

TEST_CASE("nonharmonicDb sees a weak inharmonic tone next to a harmonic series", "[support]")
{
    // 50 Hz with its 2nd harmonic (both harmonic) plus a 77 Hz tone 40 dB below the total: about -40 dB
    auto x = sig::sine(50.0, 48000.0, 1.0, -6.0);
    const auto h2 = sig::sine(100.0, 48000.0, 1.0, -12.0);
    const auto other = sig::sine(77.0, 48000.0, 1.0, -6.0 - 40.0);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] += h2[i] + other[i];
    const double total = std::pow(10.0, -6.0 / 10.0) + std::pow(10.0, -12.0 / 10.0) + std::pow(10.0, -46.0 / 10.0);
    const double expected = 10.0 * std::log10(std::pow(10.0, -46.0 / 10.0) / total);
    CHECK(std::abs(test::nonharmonicDb(x, 48000.0, 50.0, 20000.0) - expected) <= 0.05);
}

TEST_CASE("nullDb and zcFreq follow the prototype", "[support]")
{
    const auto x = sig::sine(60.0, 48000.0, 1.0, -6.0);
    CHECK(test::nullDb(x, x) == -400.0);
    auto y = x;
    for (auto& v : y)
        v *= 1.001;
    CHECK(std::abs(test::nullDb(x, y) + 60.0) <= 0.01);
    CHECK(std::abs(test::zcFreq(x, 48000.0) - 60.0) <= 1e-6);
}

TEST_CASE("C++ generators match the Python excerpts in signals.sgg within 1e-12", "[support]")
{
    const auto golden = test::Golden::load("signals.sgg");
    auto head = [](std::vector<double> v) {
        v.resize(4096);
        return v;
    };
    CHECK(maxAbsDiff(head(sig::sine(55.0, 48000.0, 0.1, -6.0, 0.3)), golden["sine"]) <= 1e-12);
    CHECK(maxAbsDiff(head(sig::logSweep(20.0, 200.0, 48000.0, 0.1)), golden["log_sweep"]) <= 1e-12);
    CHECK(maxAbsDiff(head(sig::kick(48000.0)), golden["kick"]) <= 1e-12);
    CHECK(maxAbsDiff(head(sig::bass808(44100.0)), golden["bass_808"]) <= 1e-12);
    CHECK(maxAbsDiff(head(sig::twoTone(31.0, 81.0, 48000.0, 0.1)), golden["two_tone"]) <= 1e-12);
    CHECK(maxAbsDiff(head(sig::square(40.0, 48000.0, 0.1, -6.0)), golden["square"]) <= 1e-12);
    CHECK(maxAbsDiff(sig::xorshiftNoise(4096, 7), golden["xorshift_noise"]) <= 1e-12);
    CHECK(maxAbsDiff(head(sig::bandNoise(48000.0, 0.1, 30.0, 120.0, -12.0, 4)), golden["band_noise"]) <= 1e-12);
}

TEST_CASE("the other generators follow the prototype's definitions", "[support]")
{
    CHECK(sig::impulse(8)[0] == 0.5);
    CHECK(sig::silence(48000.0, 0.01).size() == 480);
    CHECK(sig::dc(48000.0, 0.01, 0.25)[479] == 0.25);
    const auto bad = sig::nanInf(1024);
    CHECK(std::isnan(bad[128]));
    CHECK(std::isinf(bad[256]));
    CHECK(bad[512] == -std::numeric_limits<double>::infinity());
    const auto tiny = sig::denormalRamp(16);
    CHECK(std::fpclassify(tiny.back()) == FP_SUBNORMAL);
    CHECK(sig::kick(48000.0, {.nHits = 3, .spacing = 0.25}).size() == 36000);
}

// The pointers escape into this volatile global, so the compiler cannot elide the allocations it may otherwise remove
// ([expr.new]); a direct call of ::operator new is never elided.
void* volatile allocationSink = nullptr;

TEST_CASE("AllocGuard counts a vector allocation", "[support]")
{
    size_t withVector = 0;
    size_t direct = 0;
    size_t without = 0;
    {
        test::AllocGuard guard;
        std::vector<double> v(100);
        allocationSink = v.data();
        withVector = guard.count();
    }
    {
        test::AllocGuard guard;
        void* p = ::operator new(16);
        allocationSink = p;
        ::operator delete(p);
        direct = guard.count();
    }
    {
        test::AllocGuard guard;
        volatile double x = 1.0;
        x = x * 2.0;
        without = guard.count();
    }
    CHECK(withVector == 1);
    CHECK(direct == 1);
    CHECK(without == 0);
}
