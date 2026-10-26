// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/Quality.h"

namespace subgen::plugin
{
// L_total = L_os + L_look + L_lin + align (SPEC §4.4), in samples.
struct LatencyBreakdown
{
    int os = 0;    // Drive's oversampler: JUCE's filters, fixed in samples (Mix, Print)
    int look = 0;  // the engine's lookahead E plus the compressor's and the limiter's
    int lin = 0;   // Linear Phase in Print: (N_LIN - 1)/2 + 256
    int align = 0; // align_ms
    int total = 0;
};

// The latency a configuration has: depends on quality, filt_phase and align_ms only, never on the engine or the audio
// parameters. osLatency is the oversampler's (SubChain::oversamplingLatencySamples()); Live has none.
LatencyBreakdown computeLatency(Quality quality, bool linearPhase, double alignMs, double fs, int osLatency);
} // namespace subgen::plugin
