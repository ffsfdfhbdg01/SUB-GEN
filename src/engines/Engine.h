// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <span>

namespace subgen::engines
{
enum class KeyMode
{
    None,
    Detect,
    Trigger
};

enum class PhaseGen
{
    Locked,
    Free,
    Reset
};

// Physical engine controls, converted from the parameters (PARAMETERS.md §2.3-2.6); the defaults are the registry's.
struct EngineControl
{
    // common
    double sub1Gain = 1.0; // linear, Off = 0
    double sub2Gain = 0.0;
    double shape = 0.0; // 0..1
    bool follow = true; // Follow (true) or Constant
    double envAttackMs = 3.0;
    double envReleaseMs = 80.0;
    double dynamics = 0.0; // -1..1
    double confGate = 0.5; // 0..1
    bool baseFixed = false;
    double baseFreqHz = 45.0;
    double rangeLoHz = 30.0;
    double rangeHiHz = 120.0;
    int rangeSlope = 24; // dB/oct
    double detThreshDb = -55.0;
    KeyMode keyMode = KeyMode::None;
    // TRACK
    double trkSpeed = 0.5; // 0..1
    double trkGlideMs = 0.0;
    PhaseGen phaseGen = PhaseGen::Locked;
    // TRIGGER
    double trigThreshDb = -30.0;
    double trigHoldMs = 20.0;
    double trigLockoutMs = 60.0;
    double trigFStartHz = 80.0;
    double trigPitchTimeMs = 30.0;
    double trigAttackMs = 2.0;
    double trigDecayMs = 350.0;
    double trigPhaseDeg = 0.0;
    double trigVel = 0.6; // 0..1
    bool trigLegato = false;
    // HARMONIC and HYBRID
    double harmAmount = 0.5;  // 0..1
    double harmBalance = 0.0; // -1..1
    int harmMax = 4;
    double hybDivide = 1.0;
    double hybHarmonic = 0.35;
};

// The controls clamped to the registry's ranges (PARAMETERS.md), non-finite values replaced by the defaults, so an
// engine never meets values that would make its arithmetic undefined, whatever the caller passes.
EngineControl clampControl(const EngineControl& control);

// A sub generator (SPEC §6). out = sub1Gain Sub1 + sub2Gain Sub2, delayed by `lookahead` samples against `source`
// (SPEC §6.1, §4.4). prepare() may allocate; the other calls must not.
class Engine
{
public:
    virtual ~Engine() = default;
    virtual void prepare(double fs, int maxBlock, int lookahead) = 0;
    virtual void reset() = 0;
    virtual void setControl(const EngineControl& control) = 0;
    virtual void process(std::span<const double> source, std::span<const double> key, std::span<double> out) = 0;
    virtual double subFrequencyHz() const = 0;
    virtual double confidence() const = 0;
};
} // namespace subgen::engines
