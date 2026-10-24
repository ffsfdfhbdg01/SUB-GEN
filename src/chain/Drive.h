// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "chain/DriveShapes.h"
#include "core/Quality.h"
#include "dsp/DelayLine.h"
#include "dsp/Envelope.h"
#include "dsp/FilterCascade.h"
#include "dsp/Smoother.h"

namespace juce::dsp
{
template <typename SampleType>
class Oversampling;
}

namespace subgen::chain
{
struct DriveParams
{
    double drivePct = 0.0; // 0...100
    DriveMode mode = DriveMode::Soft;
    double balance = 0.0; // -100...+100
    double toneHz = 500.0;
    bool autoGain = true;
    double fSub1Hz = 40.0; // the sub's frequency: Fold cap window and Auto Gain
    // switches for tests (the plug-in leaves them on)
    bool tone = true;
    bool adaa = true;
    bool foldCap = true;
};

// Drive/Saturate (SPEC §7.3; prototype drive.process). x_ref = x 10^(12/20); u = g x_ref, g = 10^(30 drive / 100 / 20)
// (Fold: times min(1, 2 / env), env = the hold of |x_ref| over max(10 ms, 1.5 / f_sub1) with a 50 ms release, so
// inputs up to -6 dBFS are untouched); the shaper with first-order ADAA (|du| > 1e-5) at 2x (Mix, half-band IIR) or
// 4x (Print, half-band FIR) by juce::dsp::Oversampling at maximum quality and integer latency; y = (1 - alpha) x +
// alpha nl / 10^(12/20), alpha = min(1, drive / 5); the Tone y + alpha (LP_2(y) - y); the 5 Hz first-order high-pass;
// the Auto Gain. drive 0 is a pure delay by the latency (bit-exact), entered and left with a 20 ms crossfade; drive and
// balance are smoothed linearly over 20 ms, the Tone in log over 20 ms, the Auto Gain multiplicatively over 20 ms; a
// mode change crossfades over 20 ms (a further change waits for it). prepare() allocates; the other calls do not.
class Drive
{
public:
    Drive();
    ~Drive();
    Drive(const Drive&) = delete;
    Drive& operator=(const Drive&) = delete;

    void prepare(double fs, int maxBlock, Quality quality);
    void reset();
    void setParams(const DriveParams& params);
    void process(std::span<double> block);
    int latencySamples() const { return latency; }

private:
    void processChunk(std::span<double> x);
    void activate();
    double shapeSample(DriveMode mode, double u, double bias, double& previous) const;
    double foldScale(double xRef);

    double fs = 48000.0;
    int maxBlock = 512;
    int factor = 1; // oversampling
    int latency = 0;
    DriveParams params;
    bool fresh = true;   // the first parameters after prepare/reset start without ramps
    bool active = false; // the shaping path runs (drive > 0 or fading out)
    DriveMode current = DriveMode::Soft;
    DriveMode previous = DriveMode::Soft; // the mode fading out
    bool modeFading = false;

    std::unique_ptr<juce::dsp::Oversampling<double>> mixOversampler;   // 2x, half-band polyphase IIR
    std::unique_ptr<juce::dsp::Oversampling<double>> printOversampler; // 4x, half-band equiripple FIR
    juce::dsp::Oversampling<double>* oversampler = nullptr;

    dsp::DelayLine dry;         // the linear path and the bypass, delayed by the latency
    dsp::FilterCascade toneLp;  // 2nd-order Butterworth
    dsp::FilterCascade dcBlock; // 1st-order 5 Hz high-pass
    dsp::SlidingMax foldHold;
    double foldEnvelope = 0.0;
    double foldRelease = 0.0;
    double previousCurrent = 0.0; // ADAA state of each path
    double previousFading = 0.0;
    double lastToneHz = 0.0;

    dsp::LinearSmoother drive;
    dsp::LinearSmoother balance;
    dsp::LogSmoother tone;
    dsp::GainSmoother gain;
    dsp::LinearSmoother wet;  // 0: bypass, 1: shaped
    dsp::LinearSmoother fade; // the current mode's share during a mode change

    std::vector<double> u;      // base rate, then the shaped result
    std::vector<double> xDelayed;
    std::vector<double> alpha;
    std::vector<double> biasCurrent;
    std::vector<double> biasFading;
    std::vector<double> fadeWeight;
    std::vector<double> scale;
    std::vector<double> toneHz;
    std::vector<double> gains;
    std::vector<double> wets;
};
} // namespace subgen::chain
