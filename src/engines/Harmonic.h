// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <vector>

#include "dsp/DelayLine.h"
#include "dsp/Envelope.h"
#include "dsp/FilterCascade.h"
#include "engines/Engine.h"
#include "engines/PitchMpm.h"
#include "engines/PitchPrior.h"
#include "engines/SourceBand.h"

namespace subgen::engines
{
// HARMONIC (SPEC §6.5; prototype harmonic.harmonic): harmonics 2..K of the source band, so the fundamental is heard
// through them. e = the causal running maximum of |x_b| over 1.5 periods of range_lo (Fixed: of the band's lower
// edge) through a 5 / 60 ms follower; x_n = clamp(x_b / max(e, -80 dBFS), -1, 1);
// y = HP_15 Hz(e sum_{k=2..K} w_k T_k(x_n)), T_k by the recurrence T_{k+1} = 2 x T_k - T_{k-1}, w_k = amount c_k / k,
// c_k = 1 - b (even k) or 1 + b (odd k), at least 0. Output sub1_level y (Sub -2 does not apply), delayed by the
// lookahead like every engine. subFrequencyHz() is the source's f0 (Fixed: base_freq; Auto: the pitch the prior
// holds, 0 before one). prepare() allocates; process() does not.
class Harmonic final : public Engine
{
public:
    void prepare(double fs, int maxBlock, int lookahead) override;
    void reset() override;
    void setControl(const EngineControl& control) override;
    void process(std::span<const double> source, std::span<const double> key, std::span<double> out) override;
    double subFrequencyHz() const override;
    double confidence() const override { return 1.0; }

private:
    double processSample(double source);

    double fs = 48000.0;
    EngineControl control;
    std::array<double, 6> weights{}; // w_k, k = 0..5
    int harmMax = 4;

    SourceBand band;
    dsp::SlidingMax hold;
    dsp::PeakFollower follower;
    dsp::FilterCascade dcBlock;
    dsp::DelayLine delay;
    PitchMpm mpm;
    PitchPrior prior;
    std::vector<PitchFrame> frames;
    double heldF0 = 0.0;
};
} // namespace subgen::engines
