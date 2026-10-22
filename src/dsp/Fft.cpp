// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Fft.h"

#include <cassert>
#include <numbers>
#include <utility>

namespace subgen::dsp
{
namespace
{
// Plain complex product: std::complex's operator* follows C Annex G (NaN/inf recovery through a library call), which
// is slow and not needed here.
inline std::complex<double> mul(std::complex<double> a, std::complex<double> b)
{
    return {a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()};
}
} // namespace

ComplexFft::ComplexFft(int order)
    : n(1 << order)
    , twiddles(static_cast<size_t>(n / 2))
    , bitReversed(static_cast<size_t>(n))
{
    assert(order >= 0 && order < 31);
    for (int k = 0; k < n / 2; ++k)
        twiddles[static_cast<size_t>(k)] = std::polar(1.0, -2.0 * std::numbers::pi * k / n);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            r |= ((i >> b) & 1) << (order - 1 - b);
        bitReversed[static_cast<size_t>(i)] = r;
    }
}

void ComplexFft::forward(std::complex<double>* data) const
{
    transform(data, false);
}

void ComplexFft::inverse(std::complex<double>* data) const
{
    transform(data, true);
    const double scale = 1.0 / n;
    for (int i = 0; i < n; ++i)
        data[i] *= scale;
}

void ComplexFft::transform(std::complex<double>* data, bool inverse) const
{
    for (int i = 0; i < n; ++i)
    {
        const int j = bitReversed[static_cast<size_t>(i)];
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;
        for (int start = 0; start < n; start += len)
            for (int k = 0; k < half; ++k)
            {
                auto w = twiddles[static_cast<size_t>(k * step)];
                if (inverse)
                    w = std::conj(w);
                const auto t = mul(w, data[start + k + half]);
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
    }
}

RealFft::RealFft(int order)
    : fft(order)
    , scratch(static_cast<size_t>(1 << order))
{
    assert(order >= 1);
}

void RealFft::forward(const double* in, std::complex<double>* out)
{
    const int n = fft.size();
    for (int i = 0; i < n; ++i)
        scratch[static_cast<size_t>(i)] = {in[i], 0.0};
    fft.forward(scratch.data());
    for (int k = 0; k <= n / 2; ++k)
        out[k] = scratch[static_cast<size_t>(k)];
}

void RealFft::inverse(const std::complex<double>* in, double* out)
{
    const int n = fft.size();
    scratch[0] = {in[0].real(), 0.0};
    scratch[static_cast<size_t>(n / 2)] = {in[n / 2].real(), 0.0};
    for (int k = 1; k < n / 2; ++k)
    {
        scratch[static_cast<size_t>(k)] = in[k];
        scratch[static_cast<size_t>(n - k)] = std::conj(in[k]);
    }
    fft.inverse(scratch.data());
    for (int i = 0; i < n; ++i)
        out[i] = scratch[static_cast<size_t>(i)].real();
}
} // namespace subgen::dsp
