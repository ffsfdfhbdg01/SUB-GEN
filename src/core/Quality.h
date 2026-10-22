// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>

namespace subgen
{
// Quality mode (`quality`, SPEC §4.4): oversampling, lookahead and latency.
enum class Quality
{
    Live,
    Mix,
    Print
};

// round((ms / 1000) fs) with ties to even: the prototype's int(round(seconds * fs)), so the sample counts match its
// golden data (at 44.1 kHz: Mix 1984.5 -> 1984, Print 2425.5 -> 2426).
inline int msToSamples(double ms, double fs)
{
    return static_cast<int>(std::nearbyint((ms / 1000.0) * fs));
}

// Engine lookahead E (common to all engines): Live 0, Mix 45 ms, Print 55 ms.
inline int engineLookaheadSamples(Quality q, double fs)
{
    return q == Quality::Live ? 0 : msToSamples(q == Quality::Mix ? 45.0 : 55.0, fs);
}

// Compressor lookahead: 0 / 0 / 5 ms.
inline int compLookaheadSamples(Quality q, double fs)
{
    return q == Quality::Print ? msToSamples(5.0, fs) : 0;
}

// Limiter lookahead: 0 / 2 / 5 ms.
inline int limiterLookaheadSamples(Quality q, double fs)
{
    return q == Quality::Live ? 0 : msToSamples(q == Quality::Mix ? 2.0 : 5.0, fs);
}
} // namespace subgen
