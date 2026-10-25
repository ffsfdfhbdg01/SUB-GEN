// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <span>

#include "chain/LevelDetector.h"
#include "core/Quality.h"
#include "dsp/DelayLine.h"
#include "dsp/Smoother.h"

namespace subgen::chain
{
// Gain reduction (dB, <= 0) of the static curve of SPEC §7.4: L for 2(L - T) < -W; L + (1/R - 1)(L - T + W/2)^2 / (2W)
// inside the knee; T + (L - T)/R above it (W = 0: a hard knee).
double staticGainReductionDb(double levelDb, double threshDb, double ratio, double kneeDb);

struct CompParams
{
    bool on = false;
    double threshDb = -18.0;
    double ratio = 3.0;
    double kneeDb = 6.0;
    double attackMs = 10.0;
    double releaseMs = 150.0;
    bool autoRelease = true;
    Detector detector = Detector::Peak;
    double makeupDb = 0.0;
    bool autoMakeup = false;
    double fSub1Hz = 40.0;
};

// The sub path's compressor (SPEC §7.4; prototype dynamics.compressor without the 0.1 ripple rule, D7): the
// ripple-free detector, the static curve, the reduction smoothed in dB by a one-pole (attack while it deepens, release
// while it recovers; the RMS detector's attack is at least 0.25 / f_sub1), or with auto release the deeper of (attack,
// 50 ms) and (max(attack, 150 ms), 500 ms); makeup plus, with auto makeup, 0.5 |T| (1 - 1/R) dB [F3]. Print looks 5 ms
// ahead (the gain meets the input delayed by 5 ms). Off passes the input, delayed by the latency, bit-exactly;
// switching crossfades over 10 ms (raised cosine); threshold, ratio and knee are smoothed linearly over 20 ms, the
// makeup gain multiplicatively. prepare() allocates; the other calls do not.
class Compressor
{
public:
    void prepare(double fs, int maxBlock, Quality quality);
    void reset();
    void setParams(const CompParams& params);
    void process(std::span<double> block);
    double gainReductionDb() const { return reduction; }
    int latencySamples() const { return lookahead; }

private:
    double fs = 48000.0;
    int lookahead = 0;
    CompParams params;
    bool fresh = true;
    LevelDetector detector;
    dsp::DelayLine delay;
    dsp::LinearSmoother thresh;
    dsp::LinearSmoother ratio;
    dsp::LinearSmoother knee;
    dsp::GainSmoother makeup;
    dsp::LinearSmoother wet; // 0: off, 1: on
    double attack = 0.0;     // one-pole coefficients
    double release = 0.0;
    double fastRelease = 0.0;
    double slowAttack = 0.0;
    double slowRelease = 0.0;
    double single = 0.0; // the reduction of each smoother (dB)
    double fast = 0.0;
    double slow = 0.0;
    double reduction = 0.0;
};
} // namespace subgen::chain
