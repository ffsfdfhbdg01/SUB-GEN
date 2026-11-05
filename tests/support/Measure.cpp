// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/Measure.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>

#include "dsp/Fft.h"
#include "support/Dft.h"

namespace subgen::test
{
namespace
{
constexpr double kFloor = 1e-300;

// Zero-padded, Blackman-Harris-windowed magnitude spectrum, scaled so a sine's peak reads its amplitude.
struct PaddedSpectrum
{
    std::vector<double> mag;
    size_t nfft = 0;
};

PaddedSpectrum paddedSpectrum(std::span<const double> x, int pad)
{
    const size_t n = x.size();
    PaddedSpectrum s;
    s.nfft = std::bit_ceil(n * static_cast<size_t>(pad));
    std::vector<double> buf(s.nfft, 0.0);
    double wsum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double a = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n);
        const double w = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2.0 * a) - 0.01168 * std::cos(3.0 * a);
        buf[i] = x[i] * w;
        wsum += w;
    }
    dsp::RealFft fft(std::countr_zero(s.nfft));
    std::vector<std::complex<double>> spec(s.nfft / 2 + 1);
    fft.forward(buf.data(), spec.data());
    s.mag.resize(spec.size());
    for (size_t k = 0; k < spec.size(); ++k)
        s.mag[k] = std::abs(spec[k]) * 2.0 / wsum;
    return s;
}

PeakInfo findPeak(const PaddedSpectrum& s, double fs, double fLo, double fHi)
{
    const double binHz = fs / static_cast<double>(s.nfft);
    const auto first = static_cast<size_t>(std::max(0.0, std::ceil(fLo / binHz)));
    const auto last = std::min(s.mag.size() - 1, static_cast<size_t>(std::max(0.0, std::floor(fHi / binHz))));
    if (fHi < 0.0 || first > last)
        return {0.0, 0.0};
    const auto it = std::max_element(s.mag.begin() + static_cast<std::ptrdiff_t>(first),
                                     s.mag.begin() + static_cast<std::ptrdiff_t>(last) + 1);
    const auto k = static_cast<size_t>(it - s.mag.begin());
    if (k >= 1 && k + 1 < s.mag.size())
    {
        const double a = std::log(s.mag[k - 1] + kFloor);
        const double b = std::log(s.mag[k] + kFloor);
        const double c = std::log(s.mag[k + 1] + kFloor);
        const double denom = a - 2.0 * b + c;
        const double delta = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
        return {(static_cast<double>(k) + delta) * binHz, std::exp(b - 0.25 * (a - c) * delta)};
    }
    return {static_cast<double>(k) * binHz, s.mag[k]};
}
} // namespace

double rms(std::span<const double> x)
{
    if (x.empty())
        return 0.0;
    double sum = 0.0;
    for (const double v : x)
        sum += v * v;
    return std::sqrt(sum / static_cast<double>(x.size()));
}

double nullDb(std::span<const double> ref, std::span<const double> test)
{
    std::vector<double> diff(ref.size());
    for (size_t i = 0; i < ref.size(); ++i)
        diff[i] = ref[i] - test[i];
    const double num = rms(diff);
    const double den = rms(ref);
    if (num == 0.0)
        return -400.0;
    if (den == 0.0)
        return std::numeric_limits<double>::infinity();
    return std::max(-400.0, 20.0 * std::log10(num / den));
}

PeakInfo peakFreq(std::span<const double> x, double fs, double fLo, double fHi, int pad)
{
    if (x.empty())
        return {0.0, 0.0};
    return findPeak(paddedSpectrum(x, pad), fs, fLo, fHi);
}

double zcFreq(std::span<const double> x, double fs)
{
    double first = 0.0;
    double last = 0.0;
    size_t count = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0 && x[i] >= 0.0)
        {
            const double z = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (count == 0)
                first = z;
            last = z;
            ++count;
        }
    return count < 2 ? 0.0 : fs * static_cast<double>(count - 1) / (last - first);
}

std::vector<double> harmonicAmps(std::span<const double> x, double fs, double f0, int n)
{
    std::vector<double> out(static_cast<size_t>(n), 0.0);
    if (x.empty())
        return out;
    const auto spectrum = paddedSpectrum(x, 16);
    for (int k = 1; k <= n; ++k)
    {
        const double fk = k * f0;
        if (fk * 1.03 >= fs / 2.0)
            break;
        out[static_cast<size_t>(k - 1)] = findPeak(spectrum, fs, fk * 0.97, fk * 1.03).amplitude;
    }
    return out;
}

double thdDb(std::span<const double> x, double fs, double f0, int n)
{
    const auto amps = harmonicAmps(x, fs, f0, n);
    double sum = 0.0;
    for (size_t k = 1; k < amps.size(); ++k)
        sum += amps[k] * amps[k];
    const double ratio = std::sqrt(sum) / std::max(amps[0], kFloor);
    return 20.0 * std::log10(std::max(ratio, kFloor));
}

double bandDb(std::span<const double> x, double fs, double f1, double f2)
{
    const size_t n = x.size();
    if (n == 0)
        return 20.0 * std::log10(1e-15);
    const auto spec = rdft(x);
    double power = 0.0;
    for (size_t k = 0; k < spec.size(); ++k)
    {
        const double f = static_cast<double>(k) * fs / static_cast<double>(n);
        if (f < f1 || f > f2)
            continue;
        const double weight = (k == 0 || (n % 2 == 0 && k == n / 2)) ? 1.0 : 2.0;
        power += weight * std::norm(spec[k]);
    }
    power /= static_cast<double>(n) * static_cast<double>(n);
    return 20.0 * std::log10(std::max(std::sqrt(power), 1e-15));
}

double nonharmonicDb(std::span<const double> x, double fs, double f0, double fMax)
{
    const size_t n = x.size();
    const auto spec = rdft(x);
    std::vector<bool> harmonic(spec.size(), false);
    const int count = static_cast<int>(0.5 * fs / f0);
    for (int h = 1; h <= count; ++h)
    {
        const auto bin = static_cast<size_t>(std::nearbyint(h * f0 * static_cast<double>(n) / fs));
        if (bin < harmonic.size())
            harmonic[bin] = true;
    }
    double other = 0.0;
    double total = 0.0;
    for (size_t k = 0; k < spec.size(); ++k)
    {
        const double f = static_cast<double>(k) * fs / static_cast<double>(n);
        if (f < 20.0 || f > fMax)
            continue;
        const double p = std::norm(spec[k]);
        total += p;
        if (!harmonic[k])
            other += p;
    }
    return 10.0 * std::log10(std::max(other, kFloor) / total);
}
} // namespace subgen::test
