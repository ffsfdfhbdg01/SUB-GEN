// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>

#include "dsp/DelayLine.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "dsp/Smoother.h"
#include "routing/ChannelMap.h"

namespace subgen::routing
{
enum class OutMode
{
    Auto,
    Mono,
    Stereo,
    Surround,
    Lfe
};

struct RouterParams
{
    OutMode mode = OutMode::Auto;
    std::array<double, kNumClasses> classGain{1.0, 1.0, 0.5011872336272722, 0.0, 0.0}; // route_*: 0, 0, -6 dB, Off, Off
    std::array<double, kNumClasses> classDelayMs{};                                    // route_*_dly, 0...30 ms
    std::array<bool, kNumClasses> classInvert{};                                       // route_*_pol
    double width = 0.0;        // sub_width, 0...1
    double lfeTrimGain = 1.0;  // lfe_trim
    double lfeBleedGain = 0.0; // lfe_bleed (0: Off)
    // the LFE channel's filters in Surround mode, after the sub's own low-pass (SPEC_IO_UI §3)
    double lfeHpHz = 20.0;
    double lfeLpHz = 120.0;
    int lfeLpSlope = 24;
    dsp::FilterFamily family = dsp::FilterFamily::Butterworth;
};

// S7 (SPEC_IO_UI §3): out_mode resolved (Auto: LFE with an LFE channel, else Mono; LFE without one: Mono, W-01); per
// output channel u = G_class pol_class delay_class(source): Mono sends the stream sum to Front and Center; Stereo
// SL' / SR' to the front pair; Surround SL', SR' or their mean by side, the LFE channel through the LFE filters; LFE
// mode the sum at lfe_trim to the LFE channel and its copy at lfe_bleed to Front and Center. SL' = (1 - w) mid + w S1
// with w = sub_width (L20). Gains G20, delays rounded to samples (changed at block boundaries), polarity 5 ms through
// 0, a change of the resolved mode crossfades over 30 ms. Channels without sub get 0. No allocation after prepare.
class Router
{
public:
    void prepare(double fs, int maxBlock, const ChannelMap& out);
    void reset();
    void setParams(const RouterParams& params);
    // s2 empty: one stream; out: the block buffer's channels, addressed through the map's buffer indices
    void process(std::span<const double> s1, std::span<const double> s2, double* const* out, int n);
    OutMode resolved() const { return current; }

private:
    struct Sources
    {
        double left, right, mid, centre, lfe;
    };

    OutMode resolve(OutMode mode) const;
    double feed(OutMode mode, const ChannelInfo& info, const Sources& s) const;

    double fs = 48000.0;
    ChannelMap map;
    RouterParams params;
    bool fresh = true;
    OutMode current = OutMode::Mono;
    OutMode previous = OutMode::Mono;
    bool modeFading = false;
    dsp::LinearSmoother modeFade;
    std::array<dsp::GainSmoother, kNumClasses> gain;
    std::array<dsp::LinearSmoother, kNumClasses> polarity; // 0: as it is, 1: inverted
    std::array<double, kNumClasses> gainNow{};
    dsp::GainSmoother lfeTrim;
    dsp::GainSmoother lfeBleed;
    dsp::LinearSmoother width;
    double trimNow = 1.0;
    double bleedNow = 0.0;
    std::array<dsp::DelayLine, 32> delay;
    dsp::FilterCascade lfeHp;
    dsp::FilterCascade lfeLp;
};
} // namespace subgen::routing
