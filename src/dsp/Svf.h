// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <complex>

namespace subgen::dsp
{
enum class SvfMode
{
    LowPass,
    HighPass,
    BandPass, // unity gain at fc
    Bell,
    LowShelf,
    HighShelf
};

// One trapezoidal-integrator state-variable section (Simper, SvfLinearTrapOptimised2): y = m0 v0 + m1 v1 + m2 v2,
// where v0 is the input, v1 the band-pass and v2 the low-pass tap; a1 = 1/(1 + g(g + k)), a2 = g a1, a3 = g a2.
// With s' = j tan(w/2)/g: v2/v0 = 1/D, v1/v0 = s'/D, D = s'^2 + k s' + 1.
struct SvfCoeffs
{
    double g, k, a1, a2, a3, m0, m1, m2;
};

// A section from its prewarped g, damping k = 1/Q and output mix.
SvfCoeffs makeSvfSection(double g, double k, double m0, double m1, double m2);

// g = tan(pi fc/fs), k = 1/q; Bell and the shelves are the Cytomic shapes of SPEC §7.2 with A = 10^(gainDb/40)
// (gainDb is ignored by the other modes). 0 dB bell or shelf is the exact identity (m0 = 1, m1 = m2 = 0).
SvfCoeffs makeSvf(SvfMode mode, double fc, double q, double gainDb, double fs);

class SvfSection
{
public:
    // Keeps the state, so the coefficients may change every sample (cutoff modulation).
    void set(const SvfCoeffs& c) { coeffs = c; }
    const SvfCoeffs& coefficients() const { return coeffs; }
    void reset() { ic1 = ic2 = 0.0; }

    double process(double v0)
    {
        const double v3 = v0 - ic2;
        const double v1 = coeffs.a1 * ic1 + coeffs.a2 * v3;
        const double v2 = ic2 + coeffs.a2 * ic1 + coeffs.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return coeffs.m0 * v0 + coeffs.m1 * v1 + coeffs.m2 * v2;
    }

    std::complex<double> response(double f, double fs) const;

private:
    SvfCoeffs coeffs{0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0}; // identity
    double ic1 = 0.0;
    double ic2 = 0.0;
};

// First-order TPT low- or high-pass with prewarped g = tan(pi fc/fs).
struct OnePoleCoeffs
{
    double g;
    bool highPass;
};

class OnePoleSection
{
public:
    void set(const OnePoleCoeffs& c)
    {
        coeffs = c;
        gain = c.g / (1.0 + c.g);
    }
    void reset() { s = 0.0; }

    double process(double v0)
    {
        const double v = (v0 - s) * gain;
        const double lp = v + s;
        s = lp + v;
        return coeffs.highPass ? v0 - lp : lp;
    }

    std::complex<double> response(double f, double fs) const;

private:
    OnePoleCoeffs coeffs{1.0, false};
    double gain = 0.5;
    double s = 0.0;
};
} // namespace subgen::dsp
