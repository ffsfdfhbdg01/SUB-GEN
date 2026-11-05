// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <complex>
#include <vector>

namespace subgen::dsp
{
// Radix-2 complex FFT of size N = 2^order in double precision, in place. Allocates only in the constructor; the
// inverse includes the 1/N scale.
class ComplexFft
{
public:
    explicit ComplexFft(int order);

    int size() const { return n; }
    void forward(std::complex<double>* data) const;
    void inverse(std::complex<double>* data) const;

private:
    void transform(std::complex<double>* data, bool inverse) const;

    int n;
    std::vector<std::complex<double>> twiddles; // exp(-2 pi i k / N), k < N / 2
    std::vector<int> bitReversed;
};

// FFT of a real signal of size N = 2^order (order >= 1): forward gives the N/2 + 1 bins of the non-negative
// frequencies, inverse turns them back into N samples (scaled by 1/N; the imaginary parts of the DC and Nyquist bins
// are ignored, as numpy's irfft does). Allocates only in the constructor.
class RealFft
{
public:
    explicit RealFft(int order);

    int size() const { return fft.size(); }
    void forward(const double* in, std::complex<double>* out);
    void inverse(const std::complex<double>* in, double* out);

private:
    ComplexFft fft;
    std::vector<std::complex<double>> scratch;
};
} // namespace subgen::dsp
