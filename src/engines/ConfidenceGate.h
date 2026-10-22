// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace subgen::engines
{
// Confidence gate (SPEC §6.1; prototype divide.gate_states and apply_confidence_gate): opens at c >= hi =
// 0.2 + 0.6 conf_gate and closes below max(hi - 0.2, hi / 2). A provisional confidence holds it open; when that ends,
// the first real value must reach hi, otherwise the gate closes with a 10 ms release. The gain follows with a 5 ms
// attack and a 50 ms release. No allocation.
class ConfidenceGate
{
public:
    void prepare(double fs)
    {
        attack = 1.0 - std::exp(-1.0 / (5e-3 * fs));
        release = 1.0 - std::exp(-1.0 / (50e-3 * fs));
        rejectRelease = 1.0 - std::exp(-1.0 / (10e-3 * fs));
        setGate(0.5);
        reset();
    }

    void setGate(double confGate01)
    {
        hi = 0.2 + 0.6 * confGate01;
        lo = std::max(hi - 0.2, 0.5 * hi);
    }

    void reset()
    {
        state = false;
        wasProvisional = false;
        fast = false;
        gain = 0.0;
    }

    double process(double c, bool provisional)
    {
        if (provisional)
        {
            state = true;
            wasProvisional = true;
            fast = false;
        }
        else if (wasProvisional)
        {
            state = c >= hi;
            wasProvisional = false;
            fast = !state;
        }
        else if (!state && c >= hi)
        {
            state = true;
            fast = false;
        }
        else if (state && c < lo)
            state = false;
        const double target = state ? 1.0 : 0.0;
        gain += (target > gain ? attack : (fast ? rejectRelease : release)) * (target - gain);
        return gain;
    }

    bool open() const { return state; }
    double currentGain() const { return gain; }

private:
    double attack = 1.0;
    double release = 1.0;
    double rejectRelease = 1.0;
    double hi = 0.5;
    double lo = 0.3;
    bool state = false;
    bool wasProvisional = false;
    bool fast = false;
    double gain = 0.0;
};
} // namespace subgen::engines
