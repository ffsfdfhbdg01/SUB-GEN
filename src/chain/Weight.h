// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <span>

#include "dsp/Smoother.h"
#include "dsp/Svf.h"

namespace subgen::chain
{
// Weight (docs/superpowers/specs/2026-11-05-subgen-resynth-weight-design.md): a harmonic bass enhancer on the dry, in
// the manner of a "low bump with harmonics" plug-in. Two unity-gain band-passes in series, b at weight_freq (Q 1), are
// added as
// (g - 1) b, so the frequency rises by exactly weight_amount dB; the same band through an asymmetric soft saturator
// gives its 2nd and 3rd harmonics (a 10 Hz high-pass takes the saturator's DC), added at (g - 1) times a fixed share.
// 0 dB is the exact identity. The amount moves linearly in g - 1 and the frequency in log over 20 ms. One channel;
// no allocation, no latency.
class Weight
{
public:
    void prepare(double fs);
    void reset();
    void setParams(double amountDb, double freqHz, bool immediate = false); // immediate: no 20 ms ramp
    void process(std::span<double> block);

private:
    void updateFilter(double freqHz);

    double fs = 48000.0;
    dsp::LinearSmoother lift; // g - 1
    dsp::LogSmoother freq;
    dsp::SvfSection band1, band2; // two unity-gain band-passes: the bump, 12 dB/oct either side
    dsp::OnePoleSection dcBlock; // 10 Hz high-pass on the harmonics
    double madeFreq = -1.0;
};
} // namespace subgen::chain
