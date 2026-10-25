// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "dsp/Envelope.h"

namespace subgen::chain
{
enum class Detector
{
    Peak,
    Rms
};

// The ripple-free level detector of the sub path (SPEC §7.4, §7.5; prototype dynamics.detector_db) over
// w = max(10 ms, 1.5 / f_sub1), f_sub1 >= 20 Hz: Peak holds |x| over w; RMS averages x^2 over w and holds that over w,
// so a steady tone above 1/w reads a constant level. Both run all the time, so a detector change has no transient.
// Output in dB. prepare() allocates; the other calls do not.
class LevelDetector
{
public:
    void prepare(double sampleRate, int maxWindow)
    {
        fs = sampleRate;
        peakHold.prepare(maxWindow);
        rmsHold.prepare(maxWindow);
        meanSquare.prepare(maxWindow);
        set(detector, 40.0);
        reset();
    }

    void set(Detector d, double fSub1Hz)
    {
        detector = d;
        const double f = std::isfinite(fSub1Hz) ? std::max(fSub1Hz, 20.0) : 40.0;
        const int w = std::max(1, static_cast<int>(std::nearbyint(std::max(0.010, 1.5 / f) * fs)));
        if (w != window)
        {
            window = w;
            peakHold.setWindow(w);
            rmsHold.setWindow(w);
            meanSquare.setWindow(w);
        }
    }

    void reset()
    {
        peakHold.reset();
        rmsHold.reset();
        meanSquare.reset();
    }

    double processDb(double x)
    {
        const double peak = peakHold.process(std::abs(x));
        const double rms = rmsHold.process(meanSquare.process(x));
        return detector == Detector::Peak ? 20.0 * std::log10(std::max(peak, 1e-15))
                                          : 10.0 * std::log10(std::max(rms, 1e-30));
    }

    int windowSamples() const { return window; }

private:
    double fs = 48000.0;
    Detector detector = Detector::Peak;
    int window = 0;
    dsp::SlidingMax peakHold;
    dsp::SlidingMax rmsHold;
    dsp::SlidingMeanSquare meanSquare;
};
} // namespace subgen::chain
