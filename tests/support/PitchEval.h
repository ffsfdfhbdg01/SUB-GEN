// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include <limits>
#include <vector>

namespace subgen::test
{
// A pitch track sampled at frame times: t (s), f0 (Hz, NaN without an estimate) and confidence.
struct PitchTrack
{
    std::vector<double> t;
    std::vector<double> f0;
    std::vector<double> conf;
};

using PitchTruth = std::function<double(double)>; // Hz at time t (s); NaN where no note sounds

struct PitchScore
{
    double centsMedian = std::numeric_limits<double>::quiet_NaN();
    int octaveErrors = 0;
    double lockTime = std::numeric_limits<double>::quiet_NaN(); // s from the onset
    double lag = 0.0;                                           // s
    double confOpen = std::numeric_limits<double>::quiet_NaN(); // s from the onset
};

// The prototype's track.evaluate from `onset` on: the truth is shifted by the lag (0 ... 250 ms in 0.5 ms steps) that
// best explains the track where it moves faster than 500 cents/s; median |error| from the lock (3 frames in a row
// within 50 cents); octave errors = frames with conf >= 0.5 more than 600 cents off; confOpen = the first frame with
// conf >= 0.5.
PitchScore evaluatePitch(const PitchTrack& track, const PitchTruth& truth, double onset);

double median(std::vector<double> v); // numpy.median
} // namespace subgen::test
