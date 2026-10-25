// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <vector>

#include "core/Quality.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "dsp/Smoother.h"

namespace subgen::chain
{
struct SubFilterParams
{
    double hpHz = 22.0;  // sub_hp_freq
    int hpSlope = 24;    // dB/oct: 12, 24, 48, 96
    double lpHz = 140.0; // sub_lp_freq
    int lpSlope = 24;
    dsp::FilterFamily family = dsp::FilterFamily::Butterworth; // filt_type
    bool lfeMode = false;
    double lfeHpHz = 20.0;
    double lfeLpHz = 120.0;
    int lfeLpSlope = 24;
    bool linearPhase = false; // filt_phase: Task 19 (Print only, low-pass only)
};

// The sub path's high- and low-pass (SPEC §7.6): minimum-phase cascades (dsp::designFilter) of the family filt_type,
// the high-pass first; Bessel stops at 48 dB/oct. LFE mode (SPEC_IO_UI §4) replaces them with a 24 dB/oct high-pass
// at lfe_hp_freq and one low-pass at lfe_lp_freq / lfe_lp_slope, not cascaded with sub_lp. Cutoffs move in log over
// 20 ms (the coefficients follow per sample). A change of the family, a slope or the mode builds the new filters in a
// second slot, warms them up on the last 50 ms of input and crossfades to them over 20 ms (raised cosine); a change
// asked during a crossfade starts when it ends. Before the first setParams after prepare/reset the filters pass the
// signal through. groupDelayMs() is the target design's group delay (the display and Auto Align, SPEC §9); like
// Eq::curveDb, the GUI asks its own instance. No allocation after prepare.
class SubFilters
{
public:
    void prepare(double fs, int maxBlock, Quality quality);
    void reset();
    void setParams(const SubFilterParams& params);
    void process(std::span<double> block);
    int latencySamples() const { return 0; }
    double groupDelayMs(double fHz) const;

private:
    struct Shape // what a crossfade changes
    {
        dsp::FilterFamily family = dsp::FilterFamily::Butterworth;
        int hpOrder = 4;
        int lpOrder = 4;
        bool operator==(const Shape&) const = default;
    };

    struct Slot
    {
        Shape shape;
        dsp::FilterCascade hp;
        dsp::FilterCascade lp;
        double madeHp = -1.0; // the cutoffs the coefficients were made for
        double madeLp = -1.0;

        void follow(double hpHz, double lpHz, double fs);
        double process(double x) { return lp.process(hp.process(x)); }
    };

    void startChange(double hpHz, double lpHz);

    double fs = 48000.0;
    bool fresh = true;
    Shape requested;
    double hpTarget = 22.0;
    double lpTarget = 140.0;
    dsp::LogSmoother hpCutoff;
    dsp::LogSmoother lpCutoff;
    std::array<Slot, 2> slots;
    size_t active = 0;
    bool fading = false;
    dsp::LinearSmoother fade;
    std::vector<double> history; // the last 50 ms of input; the oldest sample at `written`
    size_t written = 0;
};
} // namespace subgen::chain
