// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Envelope.h"

namespace subgen::engines
{
// Source activity A (S4, SPEC §5.4; prototype activity.activity): the RMS (20 ms) of x_b turns it on at det_thresh
// and off once it has stayed 3 dB lower for longer than the 20 ms hold. process() returns the engines' mute gain:
// 1 while active, then a 20 ms release. Also provides e_pk (attack 1 ms, release 50 ms) and e_rms. No allocation.
class Activity
{
public:
    void prepare(double fs);
    void setThreshold(double db);
    void reset();

    double process(double xb)
    {
        const double e = rms.process(xb);
        peak.process(xb);
        if (!state)
        {
            if (e >= onLevel)
            {
                state = true;
                below = 0;
            }
        }
        else if (e < offLevel)
        {
            if (++below > hold)
                state = false;
        }
        else
            below = 0;
        gain = state ? 1.0 : gain * releaseCoeff;
        return gain;
    }

    bool active() const { return state; }
    double ePeak() const { return peak.current(); }
    double eRms() const { return rms.current(); }

private:
    dsp::RmsFollower rms;
    dsp::PeakFollower peak;
    double onLevel = 0.0;
    double offLevel = 0.0;
    int hold = 0;
    int below = 0;
    bool state = false;
    double gain = 0.0;
    double releaseCoeff = 0.0;
};
} // namespace subgen::engines
