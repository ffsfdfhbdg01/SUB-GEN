// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "dsp/FilterCascade.h"

namespace subgen::engines
{
// One estimate of the pitch detector.
struct PitchFrame
{
    double timeSamples = 0.0;                               // input sample at which the frame ends (and is known)
    double f0Hz = std::numeric_limits<double>::quiet_NaN(); // NaN without an estimate
    double confidence = 0.0;                                // calibrated NSDF clarity, 0..1
};

// TRACK's pitch detector (SPEC §6.3; prototype track.mpm with the prior off, see PitchPrior):
//  - x_b decimated by D = floor(fs / 2000) to fs_d = fs / D after a causal 8th-order Butterworth anti-alias low-pass
//    at 800 Hz (no filter when D = 1);
//  - a frame every 2 ms: the last W = ceil(2.5 fs_d / max(range_lo, 20)) decimated samples, zeros before the start;
//  - onset aware: the frame's leading samples below 1 % of its peak are dropped and lags are searched up to half of
//    what is left (and up to fs_d / range_lo), from max(2, floor(fs_d / fMax));
//  - NSDF computed directly, the first key maximum reaching 0.9 of the highest, parabolic interpolation:
//    f0 = fs_d / lag; confidence = clip((clarity - 0.6) / 0.4, 0, 1).
// prepare() allocates; the other calls do not.
class PitchMpm
{
public:
    void prepare(double fs, double fMaxHz);
    void setMinFrequency(double rangeLoHz); // clamped to the registry's range_lo, 15...250 Hz
    void reset();

    // Consumes x_b and writes the frames that end inside it (at most out.size()); returns their number.
    int process(std::span<const double> xb, std::span<PitchFrame> out);

    int maxFrames(int numSamples) const { return numSamples / (decim * hop) + 1; } // frames one call can produce
    int decimation() const { return decim; }
    int hopSamples() const { return decim * hop; } // input samples between frames
    double decimatedRate() const { return fsD; }
    int windowSamples() const { return window; }
    double lastDecimated() const { return lastY; } // for tests

private:
    PitchFrame analyse(std::int64_t j);
    bool pick(const double* g, int n, int tauHi, double& lag, double& height);

    int decim = 24;
    double fsD = 2000.0;
    int hop = 4;
    int tauMin = 6;
    int tauMax = 67;
    int window = 167;
    int maxWindow = 250;
    dsp::FilterCascade antiAlias;
    std::vector<double> ring;  // the last maxWindow decimated samples
    std::vector<double> frame; // scratch: the current frame
    std::vector<double> nsdf;   // scratch: its NSDF
    std::vector<double> energy; // prefix sums of g^2
    std::vector<int> peakLag;
    std::vector<double> peakHeight;
    std::int64_t inputCount = 0;
    std::int64_t decimatedCount = 0;
    double lastY = 0.0;
};
} // namespace subgen::engines
