// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <vector>

#include "dsp/Envelope.h"
#include "dsp/Smoother.h"
#include "engines/SourceBand.h"
#include "routing/ChannelMap.h"
#include "routing/Correlation.h"

namespace subgen::routing
{
enum class SourceMode
{
    Sum,
    Max,
    PerChannel,
    Auto
};

enum class KeyMode // sc_use
{
    Off,
    Detect,
    Trigger
};

struct SourceParams
{
    std::array<double, kNumClasses> feedGain{1.0, 1.0, 0.0, 0.0, 0.0}; // feed_*, linear (0: Off), in class order
    double srcGain = 1.0;
    SourceMode mode = SourceMode::Auto;
    KeyMode keyMode = KeyMode::Off;
    double keyGain = 1.0;
    double bandLoHz = 30.0; // range_lo, range_hi: the band of the correlation (SPEC §8.A)
    double bandHiHz = 120.0;
};

// S1/S2 (SPEC §5.1-5.2): the left group (left channels, the centre ones and LFE) and the right group give weighted
// means S_L and S_R (weights feed_*, G20; a mono layout gives its channel), then src_gain. Sum: one stream (S_L +
// S_R)/2; Max: the group with the larger RMS (200 ms window), switching beyond 1.5 dB with a 30 ms crossfade; Per-
// Channel: two streams; Auto: by the correlation of S_L and S_R in the source band (300 ms), Sum for rho >= +0.25, Max
// for rho <= -0.25, a linear blend between. The key bus (summed, x sc_gain) replaces the source in Detect mode and is
// kept for TRIGGER's detector in Trigger mode. sL is the first stream, sR the second (equal to sL with one). No
// allocation after prepare.
class SourceMixer
{
public:
    void prepare(double fs, int maxBlock, const ChannelMap& in);
    void reset();
    void setParams(const SourceParams& params);
    // in: the block buffer's channels, addressed through the map's buffer indices; key: keyCh channels or nullptr
    void process(const double* const* in, int n, const double* const* key, int keyCh, std::span<double> sL,
                 std::span<double> sR);
    int numStreams() const;
    double correlation() const { return corr.current(); }
    int selectedChannel() const { return selected; } // Max: 0 left, 1 right
    std::span<const double> keyStream() const { return {key.data(), keyLength}; }

private:
    double fs = 48000.0;
    ChannelMap map;
    SourceParams params;
    bool fresh = true;
    bool hasLeft = false;
    bool hasRight = false;
    std::array<dsp::GainSmoother, kNumClasses> feed;
    dsp::GainSmoother srcGain;
    dsp::GainSmoother keyGain;
    dsp::SlidingMeanSquare msLeft;
    dsp::SlidingMeanSquare msRight;
    int selected = 0;
    bool switching = false;
    dsp::LinearSmoother switchFade;
    engines::SourceBand bandLeft;
    engines::SourceBand bandRight;
    Correlation corr;
    std::vector<double> key;
    size_t keyLength = 0;
};
} // namespace subgen::routing
