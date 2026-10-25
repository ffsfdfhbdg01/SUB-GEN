// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/FirDesign.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "dsp/Fft.h"

namespace subgen::dsp
{
namespace
{
// the modified Bessel function of the first kind of order 0 (power series)
double besselI0(double x)
{
    const double q = 0.25 * x * x;
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 500 && term > 1e-17 * sum; ++k)
    {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
    }
    return sum;
}

int ceilLog2(size_t n)
{
    int e = 0;
    while ((size_t{1} << e) < n)
        ++e;
    return e;
}
} // namespace

int linearPhaseTaps(double fs)
{
    const double ratio = std::isfinite(fs) && fs > 0.0 ? fs / 48000.0 : 1.0;
    const int e = std::clamp(static_cast<int>(std::ceil(std::log2(ratio) - 1e-12)), -4, 4);
    return static_cast<int>(std::ldexp(4096.0, e)) + 1;
}

void designLinearPhaseKernel(const std::function<double(double)>& mag, double fs, std::span<double> kernel, double beta)
{
    const size_t n = kernel.size();
    if (n == 0)
        return;
    const int order = std::max(ceilLog2(n) + 2, 1);
    const size_t m = size_t{1} << order;
    const double delay = 0.5 * static_cast<double>(n - 1);
    std::vector<std::complex<double>> spectrum(m / 2 + 1);
    for (size_t k = 0; k <= m / 2; ++k)
    {
        const double f = static_cast<double>(k) * fs / static_cast<double>(m);
        spectrum[k] = std::polar(mag(f), -2.0 * std::numbers::pi * static_cast<double>(k) * delay / static_cast<double>(m));
    }
    std::vector<double> h(m);
    RealFft fft(order);
    fft.inverse(spectrum.data(), h.data());
    const double norm = besselI0(beta);
    for (size_t i = 0; i < n; ++i)
    {
        const double r = n > 1 ? 2.0 * static_cast<double>(i) / static_cast<double>(n - 1) - 1.0 : 0.0;
        kernel[i] = h[i] * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / norm;
    }
    for (size_t i = 0; i < n / 2; ++i)
        kernel[i] = kernel[n - 1 - i] = 0.5 * (kernel[i] + kernel[n - 1 - i]);
}

double kernelErrorDb(std::span<const double> kernel, const std::function<double(double)>& mag, double fs)
{
    constexpr int kOrder = 19;
    const size_t n = size_t{1} << kOrder;
    std::vector<double> padded(n, 0.0);
    std::copy_n(kernel.begin(), std::min(kernel.size(), n), padded.begin());
    std::vector<std::complex<double>> spectrum(n / 2 + 1);
    RealFft fft(kOrder);
    fft.forward(padded.data(), spectrum.data());
    double worst = 0.0;
    for (size_t k = static_cast<size_t>(std::ceil(5.0 * static_cast<double>(n) / fs)); k <= n / 2; ++k)
    {
        const double target = mag(static_cast<double>(k) * fs / static_cast<double>(n));
        worst = std::max(worst, std::abs(20.0 * std::log10(std::abs(spectrum[k]) / target)));
        if (20.0 * std::log10(target) <= -60.0)
            break;
    }
    return worst;
}
} // namespace subgen::dsp
