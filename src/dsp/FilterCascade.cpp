// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/FilterCascade.h"

#include <numbers>

namespace subgen::dsp
{
void FilterCascade::set(const CascadeDesign& design)
{
    for (int i = numSos; i < design.numSos; ++i)
        sections[static_cast<size_t>(i)].reset();
    for (int i = 0; i < design.numSos; ++i)
        sections[static_cast<size_t>(i)].set(design.sos[static_cast<size_t>(i)]);
    numSos = design.numSos;
    if (design.hasFirstOrder && !hasFirst)
        first.reset();
    hasFirst = design.hasFirstOrder;
    if (hasFirst)
        first.set(design.first);
}

void FilterCascade::reset()
{
    for (auto& s : sections)
        s.reset();
    first.reset();
}

std::complex<double> FilterCascade::response(double f, double fs) const
{
    std::complex<double> h = 1.0;
    for (int i = 0; i < numSos; ++i)
        h *= sections[static_cast<size_t>(i)].response(f, fs);
    if (hasFirst)
        h *= first.response(f, fs);
    return h;
}

double FilterCascade::groupDelaySeconds(double f, double fs) const
{
    const double step = f * 1e-5 + 1e-9;
    const auto ratio = response(f + step, fs) / response(f - step, fs);
    return -std::arg(ratio) / (2.0 * std::numbers::pi * 2.0 * step);
}
} // namespace subgen::dsp
