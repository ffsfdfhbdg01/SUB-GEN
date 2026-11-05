// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <vector>

#include "core/Quality.h"
#include "dsp/DelayLine.h"
#include "dsp/Envelope.h"
#include "dsp/Smoother.h"

namespace subgen::chain
{
// 4x interpolation for true-peak detection: a 64-tap Kaiser-windowed sinc (beta 8) at the 4x rate, cut at the base
// Nyquist frequency, 16 taps per phase, each phase of unity gain at DC. process(x) returns the four interpolated
// values of the newest input, at base times n - 7.875, n - 7.625, n - 7.375 and n - 7.125 (between samples n - 8 and
// n - 7). No allocation.
class TruePeakInterpolator
{
public:
    static constexpr int kTapsPerPhase = 16;
    static constexpr int kDelay = 8; // whole base samples behind the input

    void reset();
    std::array<double, 4> process(double x);

private:
    std::array<double, kTapsPerPhase> history{};
    int position = 0;
};

struct LimiterParams
{
    bool on = true;
    double ceilingDb = -1.0;
    double releaseMs = 100.0;
    bool truePeak = false; // Print only
};

// The sub path's limiter (SPEC §7.8; prototype dynamics.limiter). Live: y = c tanh(x / c) with first-order ADAA
// (|y| < c). Mix (2 ms) and Print (5 ms): D = round(lookahead fs); the required gain min(1, c / |x|), its minimum over
// the last D + 1 samples (gain 1 before the start), an instant drop and a one-pole release, smoothed by a Hann FIR of
// length D + 1 (sum 1) and applied to x delayed by exactly D: every tap sees a value that covers the delayed sample,
// so |y| <= c by construction. True peak (Print): the required gain from the 4x interpolation's peaks next to each
// sample, known 8 samples late, so the minimum window and the FIR are 8 samples shorter and the guarantee holds for
// the interpolated output. Off passes x delayed by D, bit-exactly; switching crossfades over 10 ms (raised cosine);
// the ceiling is smoothed linearly in dB over 20 ms. prepare() allocates; the other calls do not.
class Limiter
{
public:
    void prepare(double fs, int maxBlock, Quality quality);
    void reset();
    void setParams(const LimiterParams& params);
    void process(std::span<double> block);
    int latencySamples() const { return delay; }
    double gain() const { return lastGain; } // the gain on the last output sample (Live: y / x)
    double gainReductionDb() const;

private:
    void configure(); // window and FIR for the detection in use
    double limitLive(double x, double c);
    double limitLookahead(double x, double c);

    double fs = 48000.0;
    Quality quality = Quality::Mix;
    int delay = 0;
    int lag = 0; // the detection's delay: 0 sample peak, 8 true peak
    LimiterParams params;
    bool fresh = true;
    dsp::DelayLine dry;
    dsp::SlidingMax hold; // of -required gain: the minimum
    std::vector<double> fir;
    std::vector<double> released; // ring of the released gains, as long as the FIR
    size_t releasedPos = 0;
    double releaseState = 1.0;
    double releaseCoeff = 0.0;
    dsp::LinearSmoother ceilingDb;
    dsp::LinearSmoother wet;
    double previousX = 0.0; // Live ADAA
    TruePeakInterpolator interpolator;
    std::array<double, TruePeakInterpolator::kDelay + 1> recent{}; // the last inputs, for the interval peaks
    size_t recentPos = 0;
    double lastSegment = 0.0;
    double lastGain = 1.0;
    double lastDelayed = 0.0;
};
} // namespace subgen::chain
