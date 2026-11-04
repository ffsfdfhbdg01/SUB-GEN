// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <vector>

#include "chain/Boost.h"
#include "chain/Compressor.h"
#include "chain/Drive.h"
#include "chain/Eq.h"
#include "chain/Gate.h"
#include "chain/Limiter.h"
#include "chain/PhaseTool.h"
#include "chain/SubFilters.h"
#include "core/Quality.h"
#include "dsp/Smoother.h"

namespace subgen::chain
{
struct SubChainParams
{
    double levelGain = 0.5011872336272722; // sub_level -6 dB, linear (0: Off)
    BoostParams boost;
    GateParams gate;
    DriveParams drive;
    bool drivePostComp = false; // drive_pos
    CompParams comp;
    EqParams eq;
    SubFilterParams filters;
    PhaseParams phase;
    LimiterParams limiter;
};

// The sub path (SPEC §7) in its fixed order: Level -> Boost -> Gate -> Drive* -> Compressor -> Drive* -> EQ ->
// Filters -> Phase tool -> Limiter. The level moves multiplicatively over 20 ms (G20). Drive before or after the
// compressor (drive_pos): a change builds the other order in a second Drive and compressor, warms them up on the last
// 50 ms of their input and crossfades over 20 ms; a change during a crossfade starts when it ends. The latency is the
// sum of the stages' (Drive's oversampling, the compressor's and the limiter's lookahead, Linear Phase), fixed for a
// quality and filt_phase. A NaN or Inf in a block's output, from the input or any stage, silences that block, resets
// every stage with the parameters it had and raises nanEvent(). afterChainA() is the last block's sub after chain A
// (theta 0, no polarity, at most maxBlock samples) for Auto Align. Silent until the first setParams after
// prepare/reset. No allocation after prepare.
class SubChain
{
public:
    void prepare(double fs, int maxBlock, Quality quality);
    void reset();
    void setParams(const SubChainParams& params);
    void process(std::span<double> block);
    int latencySamples() const;
    int oversamplingLatencySamples() const { return dynamics[active].drive.latencySamples(); } // L_os
    int latencyAfterChainA() const { return limiter.latencySamples(); } // chain A's sub reaches the output this late
    bool nanEvent() const { return nanCaught; } // the last process() call caught a NaN or Inf
    // the gain the compressor and the limiter applied last, in dB (<= 0; the compressor's also while it is off)
    double compGainDb() const { return dynamics[active].comp.gainReductionDb(); }
    double limiterGainDb() const { return limiter.gainReductionDb(); }
    std::span<const double> afterChainA() const { return {chainA.data(), lastBlock}; }
    // The host renders offline (isNonRealtime): Linear Phase waits for each kernel.
    void setNonRealtime(bool offline) { filters.setNonRealtime(offline); }
    // Off the audio thread (tests, offline rendering): waits for the Linear Phase kernel asked for.
    bool waitForLinearPhase(int timeoutMs) const { return filters.waitForLinearPhase(timeoutMs); }

private:
    struct Dynamics
    {
        Drive drive;
        Compressor comp;
        bool postComp = false; // the compressor first

        void process(std::span<double> block);
    };

    void applyParams();
    void startDynamicsSwitch();
    void processChunk(std::span<double> x);

    double fs = 48000.0;
    size_t maxBlock = 64;
    SubChainParams params;
    bool fresh = true;
    dsp::GainSmoother level;
    Boost boost;
    Gate gate;
    std::array<Dynamics, 2> dynamics;
    size_t active = 0;
    bool fading = false;
    dsp::LinearSmoother fade;
    std::vector<double> history; // the last 50 ms of the dynamics' input; the oldest sample at `written`
    size_t written = 0;
    std::vector<double> scratch; // maxBlock
    Eq eq;
    SubFilters filters;
    PhaseTool phase;
    Limiter limiter;
    std::vector<double> chainA; // maxBlock
    size_t lastBlock = 0;
    bool nanCaught = false;
};
} // namespace subgen::chain
