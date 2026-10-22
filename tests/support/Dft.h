// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <complex>
#include <span>
#include <vector>

namespace subgen::test
{
// DFT of a real signal of any length, with numpy's conventions: rdft gives the n/2 + 1 bins of the non-negative
// frequencies, irdft(spectrum, n) inverts it (1/n scale; the imaginary parts of the DC and Nyquist bins are ignored).
// Radix-2 (dsp::RealFft) when n is a power of two, Bluestein's chirp-z algorithm otherwise.
std::vector<std::complex<double>> rdft(std::span<const double> x);
std::vector<double> irdft(std::span<const std::complex<double>> spectrum, size_t n);
} // namespace subgen::test
