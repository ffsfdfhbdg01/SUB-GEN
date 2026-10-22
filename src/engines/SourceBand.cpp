// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/SourceBand.h"

#include <cmath>

#include "dsp/FilterDesign.h"

namespace subgen::engines
{
using dsp::designFilter;
using dsp::FilterFamily;
using dsp::FilterKind;

void SourceBand::prepare(double sampleRate)
{
    fs = sampleRate;
    setRange(30.0, 120.0, 24);
    reset();
}

void SourceBand::setRange(double loHz, double hiHz, int slopeDb)
{
    const int order = dsp::slopeToOrder(slopeDb);
    hp.set(designFilter(FilterFamily::Butterworth, FilterKind::HighPass, order, loHz, fs));
    lp.set(designFilter(FilterFamily::Butterworth, FilterKind::LowPass, order, hiHz, fs));
}

void SourceBand::setBandpass(double centreHz)
{
    const double halfOctave = std::sqrt(2.0);
    hp.set(designFilter(FilterFamily::Butterworth, FilterKind::HighPass, 4, centreHz / halfOctave, fs));
    lp.set(designFilter(FilterFamily::Butterworth, FilterKind::LowPass, 4, centreHz * halfOctave, fs));
}

void SourceBand::reset()
{
    hp.reset();
    lp.reset();
}

double SourceBand::groupDelayMs(double fHz) const
{
    return (hp.groupDelaySeconds(fHz, fs) + lp.groupDelaySeconds(fHz, fs)) * 1000.0;
}
} // namespace subgen::engines
