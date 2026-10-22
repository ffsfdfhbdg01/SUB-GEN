// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <complex>
#include <span>

#include "dsp/FilterDesign.h"
#include "dsp/Svf.h"

namespace subgen::dsp
{
// A cascade of up to eight SVF sections and one first-order section (a CascadeDesign). No allocation.
class FilterCascade
{
public:
    // Keeps the state of the sections that stay active (a modulated cutoff); sections that become active start from 0.
    void set(const CascadeDesign& design);
    void reset();

    double process(double x)
    {
        for (int i = 0; i < numSos; ++i)
            x = sections[static_cast<size_t>(i)].process(x);
        return hasFirst ? first.process(x) : x;
    }

    void process(std::span<double> block)
    {
        for (auto& v : block)
            v = process(v);
    }

    std::complex<double> response(double f, double fs) const;

    // -d(phase)/d(omega) by a centred difference of the phase of H(f + h)/H(f - h), h = 1e-5 f + 1e-9 Hz.
    double groupDelaySeconds(double f, double fs) const;

private:
    std::array<SvfSection, 8> sections{};
    int numSos = 0;
    bool hasFirst = false;
    OnePoleSection first;
};
} // namespace subgen::dsp
