// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Svf.h"

#include <cmath>
#include <numbers>

namespace subgen::dsp
{
namespace
{
// (z - 1)/(z + 1) on the unit circle, the bilinear map of s/(2 fs).
std::complex<double> bilinearSigma(double f, double fs)
{
    return {0.0, std::tan(std::numbers::pi * f / fs)};
}
} // namespace

SvfCoeffs makeSvfSection(double g, double k, double m0, double m1, double m2)
{
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    return {g, k, a1, a2, g * a2, m0, m1, m2};
}

SvfCoeffs makeSvf(SvfMode mode, double fc, double q, double gainDb, double fs)
{
    const double t = std::tan(std::numbers::pi * fc / fs);
    const double k = 1.0 / q;
    const double a = std::pow(10.0, gainDb / 40.0);
    switch (mode)
    {
    case SvfMode::LowPass:
        return makeSvfSection(t, k, 0.0, 0.0, 1.0);
    case SvfMode::HighPass:
        return makeSvfSection(t, k, 1.0, -k, -1.0);
    case SvfMode::BandPass:
        return makeSvfSection(t, k, 0.0, k, 0.0);
    case SvfMode::Bell:
    {
        const double kb = 1.0 / (q * a);
        return makeSvfSection(t, kb, 1.0, kb * (a * a - 1.0), 0.0);
    }
    case SvfMode::LowShelf:
        return makeSvfSection(t / std::sqrt(a), k, 1.0, k * (a - 1.0), a * a - 1.0);
    case SvfMode::HighShelf:
        return makeSvfSection(t * std::sqrt(a), k, a * a, k * (1.0 - a) * a, 1.0 - a * a);
    }
    return makeSvfSection(t, k, 1.0, 0.0, 0.0);
}

std::complex<double> SvfSection::response(double f, double fs) const
{
    if (coeffs.g == 0.0)
        return coeffs.m0;
    const auto sp = bilinearSigma(f, fs) / coeffs.g;
    const auto d = sp * sp + coeffs.k * sp + 1.0;
    return (coeffs.m0 * d + coeffs.m1 * sp + coeffs.m2) / d;
}

std::complex<double> OnePoleSection::response(double f, double fs) const
{
    const auto sp = bilinearSigma(f, fs) / coeffs.g;
    return coeffs.highPass ? sp / (1.0 + sp) : 1.0 / (1.0 + sp);
}
} // namespace subgen::dsp
