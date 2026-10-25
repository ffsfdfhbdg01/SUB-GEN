// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/Smoother.h"

namespace subgen::routing
{
struct SumParams
{
    bool mixMode = false;    // sum_mode: Add or Mix
    double dryGain = 1.0;    // dry_level (Add), linear (0: Off)
    double mix = 0.5;        // mix, 0...1
    bool equalPower = true;  // mix_law
    bool wetOnly = false;    // wet_only
    double outTrimGain = 1.0; // out_trim
};

// S8 (SPEC §9.1): Add out = dry_gain d + u; Mix out = a(m) d + b(m) u (Equal Power cos/sin, Linear 1 - m / m; m = 0
// and m >= 1 - 1e-12 give exactly 1 / 0 and 0 / 1); Wet Only takes the dry away over 10 ms; out_trim last. A change of
// sum_mode crossfades over 20 ms; dry_level and out_trim move multiplicatively over 20 ms (G20), mix linearly (L20). A
// coefficient of 1 or 0 is applied exactly (no sub and unity: the dry, bit-exact); a non-finite sub sample counts as 0
// (SPEC §9.3). No allocation.
class Summation
{
public:
    void prepare(double fs);
    void reset();
    void setParams(const SumParams& params);
    // out[c] = trim (dry coefficient dry[c] + sub coefficient sub[c]) for numCh channels; the buffers may alias
    void process(const double* const* dry, const double* const* sub, double* const* out, int numCh, int n);

private:
    double fs = 48000.0;
    SumParams params;
    bool fresh = true;
    dsp::GainSmoother dryGain;
    dsp::LinearSmoother mix;
    dsp::LinearSmoother mode; // 0: Add, 1: Mix
    dsp::LinearSmoother wet;  // 0: dry, 1: wet only
    dsp::GainSmoother trim;
};
} // namespace subgen::routing
