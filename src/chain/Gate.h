// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <span>

#include "chain/LevelDetector.h"
#include "dsp/Smoother.h"

namespace subgen::chain
{
struct GateParams
{
    bool on = false;
    double threshDb = -50.0;
    double rangeDb = -40.0;
    double holdMs = 40.0;
    double releaseMs = 120.0;
    double fSub1Hz = 40.0;
};

// The sub path's gate / downward expander (SPEC §7.5; prototype dynamics.gate): the Peak detector over
// max(10 ms, 1.5 / f_sub1) opens it at the threshold; it closes once the level has stayed 3 dB below for longer than
// the hold (the detector window adds to it). The gain (0 dB open, range closed) follows in dB with a 2 ms attack and
// the release, neither faster than 0.25 / f_sub1. Off is the identity, bit-exact; switching crossfades over 10 ms
// (raised cosine); threshold and range are smoothed linearly over 20 ms. prepare() allocates; the other calls do not.
class Gate
{
public:
    void prepare(double fs, int maxBlock);
    void reset();
    void setParams(const GateParams& params);
    void process(std::span<double> block);
    bool isOpen() const { return open; }
    double gainDb() const { return gain; }

private:
    double fs = 48000.0;
    GateParams params;
    bool fresh = true;
    bool started = false; // the gain starts at the first sample's target
    LevelDetector detector;
    dsp::LinearSmoother thresh;
    dsp::LinearSmoother range;
    dsp::LinearSmoother wet;
    int hold = 0;
    int below = 0;
    bool open = false;
    double opening = 0.0; // one-pole coefficients
    double closing = 0.0;
    double gain = 0.0; // dB
};
} // namespace subgen::chain
