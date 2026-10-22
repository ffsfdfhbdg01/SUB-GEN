// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "dsp/Svf.h"

namespace subgen::dsp
{
enum class FilterFamily
{
    Butterworth,
    LinkwitzRiley,
    Bessel
};

enum class FilterKind
{
    LowPass,
    HighPass
};

// Up to eight second-order sections, then at most one first-order section. A plain value: no allocation.
struct CascadeDesign
{
    std::array<SvfCoeffs, 8> sos{};
    int numSos = 0;
    bool hasFirstOrder = false;
    OnePoleCoeffs first{1.0, false};
};

// One prewarp g = tan(pi fc/fs) for every section, as SciPy's bilinear designs.
// Butterworth: order 1..16, section Q = 1/(2 sin((2i - 1) pi / (2n))), a first-order section for odd orders.
// Linkwitz-Riley: even order 2..16, two Butterworths of half the order; their two first-order sections (LR2, LR6, ...)
// become one second-order section with k = 2, the same transfer function.
// Bessel: order 2, 4 or 8, -3 dB at fc (scipy.signal.bessel, norm 'mag'); other orders round up to the next of these.
// Out-of-range orders are clamped (this runs on the audio thread).
CascadeDesign designFilter(FilterFamily family, FilterKind kind, int order, double fc, double fs);

// Slope in dB/oct (12, 24, 48, 96) to filter order.
int slopeToOrder(int dbPerOct);
} // namespace subgen::dsp
