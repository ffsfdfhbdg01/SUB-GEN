// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/Dft.h"

#include <bit>
#include <cstdint>
#include <numbers>

#include "dsp/Fft.h"

namespace subgen::test
{
namespace
{
bool isPowerOfTwo(size_t n)
{
    return n >= 2 && std::has_single_bit(n);
}

int orderOf(size_t n)
{
    return std::countr_zero(n);
}

// Full complex DFT of any length (sign -1: forward, +1: inverse without the 1/n scale) by Bluestein's algorithm:
// X_k = c_k sum_j (x_j c_j) conj(c_{k-j}), c_j = exp(sign i pi j^2 / n), the sum a radix-2 convolution.
std::vector<std::complex<double>> bluestein(std::span<const std::complex<double>> x, int sign)
{
    const size_t n = x.size();
    const size_t m = std::bit_ceil(2 * n - 1);
    std::vector<std::complex<double>> chirp(n);
    for (size_t k = 0; k < n; ++k)
    {
        const auto k2 = static_cast<std::uint64_t>(k) * k % (2 * n); // keeps the angle exact for long inputs
        chirp[k] = std::polar(1.0, sign * std::numbers::pi * static_cast<double>(k2) / static_cast<double>(n));
    }
    std::vector<std::complex<double>> a(m), b(m);
    for (size_t k = 0; k < n; ++k)
        a[k] = x[k] * chirp[k];
    b[0] = std::conj(chirp[0]);
    for (size_t k = 1; k < n; ++k)
        b[k] = b[m - k] = std::conj(chirp[k]);
    const dsp::ComplexFft fft(orderOf(m));
    fft.forward(a.data());
    fft.forward(b.data());
    for (size_t k = 0; k < m; ++k)
        a[k] *= b[k];
    fft.inverse(a.data());
    std::vector<std::complex<double>> out(n);
    for (size_t k = 0; k < n; ++k)
        out[k] = chirp[k] * a[k];
    return out;
}
} // namespace

std::vector<std::complex<double>> rdft(std::span<const double> x)
{
    const size_t n = x.size();
    if (n == 0)
        return {};
    std::vector<std::complex<double>> out(n / 2 + 1);
    if (isPowerOfTwo(n))
    {
        dsp::RealFft fft(orderOf(n));
        fft.forward(x.data(), out.data());
        return out;
    }
    std::vector<std::complex<double>> z(x.begin(), x.end());
    const auto full = bluestein(z, -1);
    std::copy(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(out.size()), out.begin());
    return out;
}

std::vector<double> irdft(std::span<const std::complex<double>> spectrum, size_t n)
{
    std::vector<double> out(n);
    if (n == 0)
        return out;
    std::vector<std::complex<double>> half(n / 2 + 1);
    for (size_t k = 0; k < half.size() && k < spectrum.size(); ++k)
        half[k] = spectrum[k];
    half[0] = half[0].real();
    if (n % 2 == 0)
        half[n / 2] = half[n / 2].real();
    if (isPowerOfTwo(n))
    {
        dsp::RealFft fft(orderOf(n));
        fft.inverse(half.data(), out.data());
        return out;
    }
    std::vector<std::complex<double>> full(n);
    for (size_t k = 0; k < half.size(); ++k)
        full[k] = half[k];
    for (size_t k = half.size(); k < n; ++k)
        full[k] = std::conj(half[n - k]);
    const auto time = bluestein(full, +1);
    for (size_t i = 0; i < n; ++i)
        out[i] = time[i].real() / static_cast<double>(n);
    return out;
}
} // namespace subgen::test
