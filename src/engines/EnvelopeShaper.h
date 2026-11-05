// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "core/Quality.h"
#include "dsp/Envelope.h"

namespace subgen::engines
{
// Sub amplitude envelope (SPEC §6.1 `follow`, `env_attack`, `env_release`, `dynamics`): Follow smooths the source
// level (e_rms) with the attack and release times; Constant is -12 dBFS peak (the activity gates it elsewhere).
// `dynamics` d in [-1, 1] is the [F3] macro; initial mapping, to be tuned by ear in Phase 5:
//   release x 4^d; attack x (1 + 3 max(0, d)); for d < 0 a transient accent (-d) max(0, e_pk - e_rms) for 5 ms after
//   an onset. d = 0 is the plain follower. No allocation.
class EnvelopeShaper
{
public:
    static double constantLevel() { return std::pow(10.0, -12.0 / 20.0); }

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        accentLength = msToSamples(5.0, fs);
        set(true, 3.0, 80.0, 0.0);
        reset();
    }

    void set(bool followSource, double attackMs, double releaseMs, double dynamicsAmount)
    {
        follow = followSource;
        d = std::clamp(dynamicsAmount, -1.0, 1.0);
        follower.setTimes(fs, attackMs * (1.0 + 3.0 * std::max(0.0, d)), releaseMs * std::pow(4.0, d));
    }

    void reset()
    {
        follower.reset();
        accentLeft = 0;
    }

    double process(double level, double ePeak, double eRms, bool onset)
    {
        if (onset)
            accentLeft = accentLength;
        const double env = follower.process(level);
        double accent = 0.0;
        if (accentLeft > 0)
        {
            --accentLeft;
            if (d < 0.0)
                accent = -d * std::max(0.0, ePeak - eRms);
        }
        return follow ? env + accent : constantLevel();
    }

private:
    double fs = 48000.0;
    bool follow = true;
    double d = 0.0;
    int accentLength = 0;
    int accentLeft = 0;
    dsp::PeakFollower follower;
};
} // namespace subgen::engines
