// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::dsp
{
namespace
{
struct BesselSection
{
    double w0; // pole magnitude (normalised to -3 dB at 1 rad/s)
    double k;  // 1/Q = 2 (-Re p) / |p|
};

// Poles of scipy.signal.bessel(n, 1, analog=True, norm='mag'), upper half plane, sorted by magnitude.
constexpr std::array<BesselSection, 1> kBessel2{{{1.2720196495140692, 1.7320508075688774}}};
constexpr std::array<BesselSection, 2> kBessel4{{{1.4301715599939901, 1.9159489237182163},
                                                 {1.6033575162169724, 1.2414059300989957}}};
constexpr std::array<BesselSection, 4> kBessel8{{{1.7784659117746597, 1.9763194658569603},
                                                 {1.8320926011985934, 1.7869614418572175},
                                                 {1.953195759022076, 1.4067624417997209},
                                                 {2.1887262305274517, 0.8158806765266106}}};

SvfCoeffs section(FilterKind kind, double g, double k)
{
    return kind == FilterKind::LowPass ? makeSvfSection(g, k, 0.0, 0.0, 1.0) : makeSvfSection(g, k, 1.0, -k, -1.0);
}

void addButterworth(CascadeDesign& d, FilterKind kind, int order, double g)
{
    for (int i = 1; i <= order / 2; ++i)
    {
        const double q = 1.0 / (2.0 * std::sin((2 * i - 1) * std::numbers::pi / (2 * order)));
        d.sos[static_cast<size_t>(d.numSos++)] = section(kind, g, 1.0 / q);
    }
}

template <size_t N>
void addBessel(CascadeDesign& d, FilterKind kind, const std::array<BesselSection, N>& table, double t)
{
    for (const auto& s : table)
        d.sos[static_cast<size_t>(d.numSos++)] = section(kind, kind == FilterKind::LowPass ? s.w0 * t : t / s.w0, s.k);
}
} // namespace

CascadeDesign designFilter(FilterFamily family, FilterKind kind, int order, double fc, double fs)
{
    CascadeDesign d;
    const double g = std::tan(std::numbers::pi * fc / fs);
    switch (family)
    {
    case FilterFamily::Butterworth:
    {
        const int n = std::clamp(order, 1, 16);
        addButterworth(d, kind, n, g);
        if (n % 2 == 1)
        {
            d.hasFirstOrder = true;
            d.first = {g, kind == FilterKind::HighPass};
        }
        break;
    }
    case FilterFamily::LinkwitzRiley:
    {
        const int half = std::clamp((order + 1) / 2, 1, 8);
        addButterworth(d, kind, half, g);
        addButterworth(d, kind, half, g);
        if (half % 2 == 1)
            d.sos[static_cast<size_t>(d.numSos++)] = section(kind, g, 2.0); // first-order section squared
        break;
    }
    case FilterFamily::Bessel:
        if (order <= 2)
            addBessel(d, kind, kBessel2, g);
        else if (order <= 4)
            addBessel(d, kind, kBessel4, g);
        else
            addBessel(d, kind, kBessel8, g);
        break;
    }
    return d;
}

int slopeToOrder(int dbPerOct)
{
    return dbPerOct / 6;
}
} // namespace subgen::dsp
