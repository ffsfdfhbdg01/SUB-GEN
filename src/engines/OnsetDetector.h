// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include "dsp/Envelope.h"

namespace subgen::engines
{
// Transient onsets (SPEC §6.4; prototype trigger.detect_onsets). A fast envelope of |x| (attack 0.2 ms, release
// 15 ms) crossing trig_thresh upwards fires when the detector is armed and trig_lockout has passed since the last
// event. It is armed at the start and again once the envelope has stayed 3 dB under the threshold for trig_hold (at
// least one sample). The event time interpolates the crossing linearly between the two samples. No allocation.
class OnsetDetector
{
public:
    void prepare(double fs);
    void set(double threshDb, double holdMs, double lockoutMs);
    void reset();

    // The onset's offset from the current sample, in (-1, 0], when one fires at it.
    std::optional<double> process(double x);

private:
    double fs = 48000.0;
    dsp::PeakFollower follower;
    double hi = 0.0316;
    double lo = 0.0224;
    int hold = 960;
    double lockout = 2880.0; // samples
    std::int64_t n = 0;
    double previous = 0.0;
    int below = 0;
    bool armed = true;
    double lastTime = -std::numeric_limits<double>::infinity();
};
} // namespace subgen::engines
