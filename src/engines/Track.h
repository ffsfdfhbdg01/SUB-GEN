// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "dsp/Envelope.h"
#include "engines/Activity.h"
#include "engines/ConfidenceGate.h"
#include "engines/Engine.h"
#include "engines/EnvelopeShaper.h"
#include "engines/NoteTracker.h"
#include "engines/OnsetDetector.h"
#include "engines/PitchMpm.h"
#include "engines/PitchPrior.h"
#include "engines/Pll.h"
#include "engines/SourceBand.h"

namespace subgen::engines
{
// One output sample's values (for tests).
struct TrackProbe
{
    double f0 = 0.0;         // the pitch track read for this sample (Hz; 0 without one)
    double conf = 0.0;       // the confidence read for this sample
    bool pitchValid = false; // the prior holds a track
    double gate = 0.0;       // confidence-gate gain (1 in Fixed)
    double envelope = 0.0;   // amplitude x activity mute gain x gate gain x the oscillator's fade-in
    bool ready = false;      // the oscillator sounds with the gate open (Fixed: always)
    double phase1 = 0.0;     // Sub -1 oscillator phase, rad in [0, 2 pi)
    double subFreq = 0.0;    // Sub -1 frequency, Hz (0 without a pitch)
};

// TRACK (SPEC §6.3). Analysis on the current input: source band, activity, note boundaries, MPM frames and the
// continuity prior. Synthesis on the axis delayed by the lookahead E: pitch and confidence are the newest frame's
// (E ahead of the output), the amplitude is the source's at the output sample (read ahead it would start the sub
// before the dry). The amplitude (Follow) follows the 1.5-period peak of x_b (range_lo) with env_attack / env_release,
// as the prototype's amplitude_envelope; Constant is -12 dBFS; both are gated by the activity.
// Phase (phase_gen):
//  - Locked: a PLL on the delayed x_b, seeded with the pitch at each note start; phi_sub = phi_PLL / 2 + c0, c0 such
//    that the sub starts with the sign of the note's first half-wave; loop bandwidth 5 Hz, 20 Hz while the pitch
//    moves more than a semitone in 20 ms or the loop's own phase error, low-passed (2 x 10 ms), exceeds 0.45 rad
//    (while it stays above 0.15 rad; and 20 ms after either);
//  - Free: accumulation of the pitch smoothed in log f (time constant 80 (5/80)^trk_speed ms) after the glide (a jump
//    of more than a semitone glides linearly in log f over trk_glide);
//  - Reset: Free with the phases set to 0 at the onsets of SPEC §6.4 (trig_thresh, trig_hold, trig_lockout; the key
//    with key_mode Trigger), at their fractional time.
// Fixed: a drone at base_freq (Sub -2 at base_freq / 2) gated by the activity, without the confidence gate.
// prepare() allocates; process() does not.
class Track final : public Engine
{
public:
    void prepare(double fs, int maxBlock, int lookahead) override;
    void reset() override;
    void setControl(const EngineControl& control) override;
    void process(std::span<const double> source, std::span<const double> key, std::span<double> out) override;
    double subFrequencyHz() const override { return lastProbe.subFreq; }
    double confidence() const override { return lastProbe.conf; }
    bool gateOpen() const override { return control.baseFixed || gate.open(); }

    // The last note's delay from its start to the gate opening on the output (0 when the gate was open in time:
    // Mix and Print for notes the lookahead covers; Live: about two periods).
    double onsetDelayMs() const { return reportedDelayMs; }
    std::int64_t lastNoteBegin() const { return noteBegin; } // input sample of the last note start (-1: none)
    double lastOnsetTime() const { return lastOnset; }      // input samples of the last onset applied (NaN: none)
    const TrackProbe& probe() const { return lastProbe; }     // for tests

private:
    double processSample(double source, double key);
    void analyse(std::int64_t cur, double xb, double key);
    void seedPll(std::int64_t m, std::int64_t cur, double f0);
    double freeFrequency();
    double ringX(std::int64_t i) const { return xbRing[static_cast<size_t>(i) % xbRing.size()]; }

    double fs = 48000.0;
    int look = 0;
    int history = 0; // x_b kept for the PLL seeds: the lookahead plus 250 ms
    EngineControl control;
    double speedCoeff = 0.0;
    int glideSamples = 0;
    int glideFrames = 10; // 20 ms of frames
    int wideHold = 960;   // 20 ms
    double pllAmpCoeff = 0.0;

    SourceBand band;
    Activity activity;
    NoteTracker notes;
    OnsetDetector onsets;
    ConfidenceGate gate;
    EnvelopeShaper shaper;
    dsp::SlidingMax peakHold;
    PitchMpm mpm;
    PitchPrior prior;
    Pll pll;

    std::vector<PitchFrame> frames;
    std::vector<double> xbRing;
    std::vector<double> ampRing;   // amplitude x activity mute gain, delayed by E
    std::vector<double> frameLogF; // the read log f of the last glideFrames + 1 frames (NaN without a track)
    std::array<double, 16> onsetQueue{}; // onsets (input samples) not yet reached by the delayed axis
    int onsetCount = 0;
    double lastOnset = 0.0;

    std::int64_t n = 0;
    std::int64_t frameCount = 0;
    double latestF0 = 0.0;
    double latestConf = 0.0;
    bool latestTracking = false;
    std::int64_t wideUntil = -1;
    bool wide = false;

    std::int64_t pendingBegin = -1; // a note start not yet reached by the delayed axis
    std::int64_t noteBegin = -1;    // the current note's start
    bool seedPending = false;
    int mismatchSamples = 0; // the frames' pitch has been a semitone or more off the loop's for this long
    bool pllSeeded = false;
    double pllA2 = 0.0;
    double lockCoeff = 0.0;
    double lockError1 = 0.0; // the PLL's phase error through two 10 ms one-poles
    double lockError2 = 0.0;
    bool lockLost = false;
    std::int64_t reportBegin = -1;
    double reportedDelayMs = 0.0;

    bool wasTracking = false;
    bool sounding = false; // the oscillator has a pitch (from a track's first value until the gate has closed)
    double voice = 0.0;    // its 1 ms fade-in
    double voiceCoeff = 0.0;
    double heldF1 = 0.0;
    double glideLogF = 0.0;
    double speedLogF = 0.0;
    double lastLogF = 0.0;
    int rampLeft = 0;
    double phase1 = 0.0;
    double phase2 = 0.0;
    TrackProbe lastProbe;
};
} // namespace subgen::engines
