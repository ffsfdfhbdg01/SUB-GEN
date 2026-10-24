// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <limits>

namespace subgen::engines
{
// Continuity prior of TRACK's pitch track (SPEC §6.3; prototype track.apply_prior), one call per detector frame. A
// value within +-25 % of the accepted one is taken at once; any other only after 3 consecutive frames that agree with
// each other (+-25 %, octave jumps included); the same rule seeds the first value, so a lone outlier cannot start the
// track. Frames with confidence below 0.5 (or without an estimate) are ignored and the last value is held (NaN before
// the first one); 5 of them in a row forget the track. No allocation.
class PitchPrior
{
public:
    double update(double f0, double conf)
    {
        if (!(conf >= kVoiced && std::isfinite(f0)))
        {
            if (quiet < kLostFrames)
                ++quiet;
            if (quiet >= kLostFrames)
            {
                hasCurrent = false;
                pending = 0;
            }
            return last;
        }
        quiet = 0;
        if (hasCurrent && std::abs(f0 / current - 1.0) <= kWindow)
        {
            current = f0;
            pending = 0;
        }
        else
        {
            pending = pending != 0 && std::abs(f0 / pendingValue - 1.0) <= kWindow ? pending + 1 : 1;
            pendingValue = f0;
            if (pending >= kConfirm)
            {
                current = f0;
                hasCurrent = true;
                pending = 0;
            }
        }
        if (hasCurrent)
            last = current;
        return last;
    }

    void reset() { *this = PitchPrior{}; }

private:
    static constexpr double kWindow = 0.25;
    static constexpr int kConfirm = 3;
    static constexpr double kVoiced = 0.5;
    static constexpr int kLostFrames = 5;

    bool hasCurrent = false;
    double current = 0.0;
    double last = std::numeric_limits<double>::quiet_NaN();
    int pending = 0;
    double pendingValue = 0.0;
    int quiet = 0;
};
} // namespace subgen::engines
