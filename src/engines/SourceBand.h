// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/FilterCascade.h"

namespace subgen::engines
{
// Source band x_b (S3, SPEC §5.3): Butterworth high-pass at range_lo, then low-pass at range_hi (TPT, minimum
// phase), each of the order of range_slope; Fixed mode: +-1/2 octave around the centre, 24 dB/oct. The settings keep
// the filter state, so the band may follow smoothed values every sample. No allocation.
class SourceBand
{
public:
    void prepare(double sampleRate);
    void setRange(double loHz, double hiHz, int slopeDb);
    void setBandpass(double centreHz);
    void reset();

    double process(double x) { return lp.process(hp.process(x)); }

    double groupDelayMs(double fHz) const;

private:
    double fs = 48000.0;
    dsp::FilterCascade hp;
    dsp::FilterCascade lp;
};
} // namespace subgen::engines
