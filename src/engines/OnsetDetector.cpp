// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/OnsetDetector.h"

#include <algorithm>
#include <cmath>

#include "core/Quality.h"

namespace subgen::engines
{
namespace
{
constexpr double kHysteresisDb = 3.0;
} // namespace

void OnsetDetector::prepare(double sampleRate)
{
    fs = sampleRate;
    follower.prepare(fs, 0.2, 15.0);
    set(-30.0, 20.0, 60.0);
    reset();
}

void OnsetDetector::set(double threshDb, double holdMs, double lockoutMs)
{
    hi = std::pow(10.0, threshDb / 20.0);
    lo = hi * std::pow(10.0, -kHysteresisDb / 20.0);
    hold = std::max(0, msToSamples(holdMs, fs));
    lockout = lockoutMs * 1e-3 * fs;
}

void OnsetDetector::reset()
{
    follower.reset();
    n = 0;
    previous = 0.0;
    below = 0;
    armed = true;
    lastTime = -std::numeric_limits<double>::infinity();
}

std::optional<double> OnsetDetector::process(double x)
{
    const double e = follower.process(x);
    const std::int64_t i = n++;
    std::optional<double> onset;
    if (i >= 1 && previous < hi && e >= hi)
    {
        const double t = static_cast<double>(i - 1) + (hi - previous) / (e - previous);
        if (armed && t - lastTime >= lockout)
        {
            armed = false;
            lastTime = t;
            onset = t - static_cast<double>(i);
        }
    }
    // a dip under the threshold - 3 dB re-arms once it has lasted the hold time
    if (e < lo)
    {
        if (++below >= std::max(hold, 1))
            armed = true;
    }
    else
        below = 0;
    previous = e;
    return onset;
}
} // namespace subgen::engines
