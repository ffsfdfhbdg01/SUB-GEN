// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugin/Latency.h"

#include <algorithm>
#include <cmath>

#include "dsp/FirDesign.h"

namespace subgen::plugin
{
LatencyBreakdown computeLatency(Quality quality, bool linearPhase, double alignMs, double fs, int osLatency)
{
    LatencyBreakdown b;
    b.os = quality == Quality::Live ? 0 : std::max(osLatency, 0);
    b.look = engineLookaheadSamples(quality, fs) + compLookaheadSamples(quality, fs) + limiterLookaheadSamples(quality, fs);
    b.lin = quality == Quality::Print && linearPhase ? (dsp::linearPhaseTaps(fs) - 1) / 2 + 256 : 0;
    b.align = msToSamples(std::isfinite(alignMs) ? std::clamp(alignMs, 0.0, 40.0) : 0.0, fs);
    b.total = b.os + b.look + b.lin + b.align;
    return b;
}
} // namespace subgen::plugin
