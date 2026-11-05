// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>

#include "dsp/Smoother.h"
#include "dsp/Svf.h"

namespace subgen::chain
{
enum class BoostType
{
    Peak,
    LowShelf
};

struct BoostParams
{
    double gainDb = 0.0; // 0...+18
    BoostType type = BoostType::Peak;
    double freqHz = 55.0;
    bool follow = true;
    double q = 1.0;
    double fSub1Hz = 55.0;       // the engine's Sub -1 frequency
    bool engineHasPitch = true;  // false for HARMONIC: the centre is boost_freq
};

// Frequency Boost (SPEC §7.2): a Cytomic peak or low shelf (dsp::makeSvf) at boost_freq or, with follow, at the Sub -1
// frequency (an f_sub1 that is not a frequency holds the last centre; before the first one, boost_freq); the centre and
// Q move in log over 20 ms, the gain multiplicatively over 20 ms; 0 dB is the exact identity. Both shapes run all the
// time, so a type change crossfades over 20 ms (raised cosine) without a transient; a change asked during a crossfade
// starts when it ends. Before the first setParams after prepare/reset the Boost passes the signal through. No
// allocation.
class Boost
{
public:
    void prepare(double fs);
    void reset();
    void setParams(const BoostParams& params);
    void process(std::span<double> block);
    double centreHz() const { return centre.current(); }

private:
    void updateCoefficients(double gain, double fc, double q);

    double fs = 48000.0;
    bool fresh = true;
    bool heardSub = false; // a valid f_sub1 came since prepare/reset
    double centreTarget = 55.0;
    dsp::GainSmoother gain;
    dsp::LogSmoother centre;
    dsp::LogSmoother q;
    dsp::LinearSmoother fade;
    std::array<dsp::SvfSection, 2> sections; // Peak, LowShelf
    BoostType requested = BoostType::Peak;
    BoostType current = BoostType::Peak;
    BoostType previous = BoostType::Peak;
    bool fading = false;
    double madeGain = -1.0; // the values the coefficients were made from
    double madeCentre = -1.0;
    double madeQ = -1.0;
};
} // namespace subgen::chain
