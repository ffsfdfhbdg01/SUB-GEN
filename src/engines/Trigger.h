// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "dsp/Envelope.h"
#include "engines/Engine.h"
#include "engines/OnsetDetector.h"
#include "engines/PitchMpm.h"
#include "engines/PitchPrior.h"
#include "engines/SourceBand.h"

namespace subgen::engines
{
// One output sample's values (for tests).
struct TriggerProbe
{
    bool onset = false;     // a voice starts at this sample
    double onsetTime = 0.0; // its onset, in input samples (fractional)
    double fEnd = 0.0;      // its end frequency, Hz
    double velocity = 0.0;  // its velocity V
    int voices = 0;         // voices sounding at this sample
};

// TRIGGER (SPEC §6.4; prototype trigger.render). Onsets of x_b (or of the key with key_mode Trigger) start a
// closed-form voice at their fractional time on the axis delayed by the lookahead:
//   phi(s) = phi0 + 2 pi (f_end s + (f_start - f_end) tau_p (1 - exp(-s / tau_p))), Sub -2 at half the frequency;
//   A(s) = a0 + (V - a0) s / t_a for s < t_a = max(trig_attack, 0.5 ms), then V 10^(-3 (s - t_a) / T60);
//   V = clamp((peak / 0.5)^trig_vel, 0, 1.5), peak = max |x_b| in the 20 ms from the onset (Mix and Print know it
//   when the voice starts; Live uses the peak so far, final after 20 ms).
// Follow multiplies the voice by e_rms (20 ms) over its running maximum since the onset, with 1 dB of tolerance, never
// rising again. Restart: a new voice from phi0, the old one fades out over 3 ms (cos^2) from the new onset; Legato:
// phase and amplitude carry on (a0 = the voice's level at the new onset), the pitch starts again from f_start.
// f_end: base_freq (Fixed); Auto: half the pitch the prior holds at the trigger (its last track, as of the newest
// frame; 20...100 Hz), base_freq before any track. prepare() allocates; process() does not.
class Trigger final : public Engine
{
public:
    void prepare(double fs, int maxBlock, int lookahead) override;
    void reset() override;
    void setControl(const EngineControl& control) override;
    void process(std::span<const double> source, std::span<const double> key, std::span<double> out) override;
    double subFrequencyHz() const override { return lastFrequency; }
    double confidence() const override { return lastProbe.voices > 0 ? 1.0 : 0.0; }

    const TriggerProbe& probe() const { return lastProbe; } // for tests

private:
    struct Pending // an onset on the current axis, waiting for the delayed one
    {
        double time = 0.0;
        double peak = 0.0;
        int remaining = 0; // samples of the velocity window still to come
    };

    struct Voice
    {
        bool started = false;  // holds a state (Legato carries it on)
        bool sounding = false; // still audible
        double onset = 0.0;    // input samples
        double phi1 = 0.0;
        double phi2 = 0.0;
        double from = 0.0; // attack start level
        double peak = 0.0; // velocity window
        int remaining = 0;
        double velocity = 1.0;
        double fEnd = 45.0;
        double runMax = 0.0; // Follow
        double gain = 1.0;
        bool fading = false; // Restart: fades out from fadeFrom
        double fadeFrom = 0.0;
    };

    double processSample(double source, double key);
    void startVoice(std::int64_t m, const Pending& p);
    double level(const Voice& v, double s) const;
    double phaseAdvance(const Voice& v, double s) const;
    double velocityOf(double peak) const;
    void render(Voice& v, std::int64_t m, double e, double& y1, double& y2);

    double fs = 48000.0;
    int look = 0;
    EngineControl control;
    double attack = 0.002;
    double t60 = 0.35;
    double tau = 0.03;
    double phase0 = 0.0;
    int peakWindow = 960;
    int fadeSamples = 144;

    SourceBand band;
    OnsetDetector detector;
    dsp::RmsFollower rms;
    PitchMpm mpm;
    PitchPrior prior;
    std::vector<PitchFrame> frames;
    std::vector<double> rmsRing; // e_rms, delayed by E (and one sample more)
    std::array<Pending, 16> pending{};
    int pendingCount = 0;
    Voice current;
    Voice fading;

    std::int64_t n = 0;
    double latestF0 = 0.0; // the prior's held pitch (0: none yet)
    double lastFrequency = 0.0;
    TriggerProbe lastProbe;
};
} // namespace subgen::engines
