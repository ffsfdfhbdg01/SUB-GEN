// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>

#include "dsp/Smoother.h"
#include "dsp/Svf.h"

namespace subgen::chain
{
enum class EqType
{
    Bell,
    LowShelf,
    HighShelf
};

struct EqBand
{
    bool on = false;
    EqType type = EqType::Bell;
    double freqHz = 100.0;
    double gainDb = 0.0;
    double q = 1.0; // for the shelves: the slope
};

struct EqParams
{
    // the registry's defaults (PARAMETERS §2.10): all off, 0 dB
    std::array<EqBand, 5> bands{EqBand{false, EqType::LowShelf, 40.0, 0.0, 0.71}, EqBand{false, EqType::Bell, 60.0, 0.0, 1.0},
                                EqBand{false, EqType::Bell, 100.0, 0.0, 1.0}, EqBand{false, EqType::Bell, 160.0, 0.0, 1.0},
                                EqBand{false, EqType::HighShelf, 300.0, 0.0, 0.71}};
};

// The 5-band parametric EQ of the sub path (SPEC §7.2): Cytomic bell, low and high shelf (dsp::makeSvf), bands in
// series 1 -> 5. A band off or at 0 dB is the exact identity, also through its crossfades. Frequency and Q move in log
// over 20 ms, the gain multiplicatively over 20 ms; switching a band crossfades over 10 ms and changing its type over
// 20 ms (raised cosine; every type's section runs all the time, so there is no transient; a change asked during a
// crossfade starts when it ends). Before the first setParams after prepare/reset the EQ passes the signal through.
// curveDb() sums the bands' responses in dB from the current coefficients: a GUI-side Eq given the same parameters
// (prepare + setParams) draws the curve without touching the audio thread's instance. No allocation.
class Eq
{
public:
    void prepare(double fs);
    void reset();
    void setParams(const EqParams& params);
    void process(std::span<double> block);
    double curveDb(double fHz) const;

private:
    struct Band
    {
        dsp::GainSmoother gain;
        dsp::LogSmoother freq;
        dsp::LogSmoother q;
        dsp::LinearSmoother onFade;
        dsp::LinearSmoother typeFade;
        std::array<dsp::SvfSection, 3> sections; // Bell, LowShelf, HighShelf
        EqType requested = EqType::Bell;
        EqType current = EqType::Bell;
        EqType previous = EqType::Bell;
        bool typeFading = false;
        bool on = false;
        double madeGain = -1.0;
        double madeFreq = -1.0;
        double madeQ = -1.0;
    };

    void updateCoefficients(Band& b, double gain, double f, double q) const;

    double fs = 48000.0;
    bool fresh = true;
    std::array<Band, 5> bands;
};
} // namespace subgen::chain
