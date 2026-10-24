// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/Engine.h"

#include <algorithm>
#include <cmath>

namespace subgen::engines
{
namespace
{
double clampFinite(double v, double lo, double hi, double fallback)
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}
} // namespace

EngineControl clampControl(const EngineControl& in)
{
    const EngineControl def;
    EngineControl c = in;
    const double maxGain = std::pow(10.0, 12.0 / 20.0); // +12 dB; Off (-60 dB) is 0
    c.sub1Gain = clampFinite(c.sub1Gain, 0.0, maxGain, def.sub1Gain);
    c.sub2Gain = clampFinite(c.sub2Gain, 0.0, maxGain, def.sub2Gain);
    c.shape = clampFinite(c.shape, 0.0, 1.0, def.shape);
    c.envAttackMs = clampFinite(c.envAttackMs, 0.1, 50.0, def.envAttackMs);
    c.envReleaseMs = clampFinite(c.envReleaseMs, 5.0, 1000.0, def.envReleaseMs);
    c.dynamics = clampFinite(c.dynamics, -1.0, 1.0, def.dynamics);
    c.confGate = clampFinite(c.confGate, 0.0, 1.0, def.confGate);
    c.baseFreqHz = clampFinite(c.baseFreqHz, 20.0, 200.0, def.baseFreqHz);
    c.rangeHiHz = clampFinite(c.rangeHiHz, 30.0, 500.0, def.rangeHiHz);
    // range_hi >= 1.2 range_lo, the first parameter of the relation wins (PARAMETERS.md, as sanitise() does)
    c.rangeLoHz = std::min(clampFinite(c.rangeLoHz, 15.0, 250.0, def.rangeLoHz), c.rangeHiHz / 1.2);
    if (c.rangeSlope != 12 && c.rangeSlope != 24 && c.rangeSlope != 48)
        c.rangeSlope = def.rangeSlope;
    c.detThreshDb = clampFinite(c.detThreshDb, -90.0, 0.0, def.detThreshDb);
    c.trkSpeed = clampFinite(c.trkSpeed, 0.0, 1.0, def.trkSpeed);
    c.trkGlideMs = clampFinite(c.trkGlideMs, 0.0, 200.0, def.trkGlideMs);
    c.trigThreshDb = clampFinite(c.trigThreshDb, -60.0, 0.0, def.trigThreshDb);
    c.trigHoldMs = clampFinite(c.trigHoldMs, 0.0, 200.0, def.trigHoldMs);
    c.trigLockoutMs = clampFinite(c.trigLockoutMs, 5.0, 500.0, def.trigLockoutMs);
    c.trigFStartHz = clampFinite(c.trigFStartHz, 30.0, 200.0, def.trigFStartHz);
    c.trigPitchTimeMs = clampFinite(c.trigPitchTimeMs, 1.0, 200.0, def.trigPitchTimeMs);
    c.trigAttackMs = clampFinite(c.trigAttackMs, 0.0, 20.0, def.trigAttackMs);
    c.trigDecayMs = clampFinite(c.trigDecayMs, 20.0, 3000.0, def.trigDecayMs);
    c.trigPhaseDeg = clampFinite(c.trigPhaseDeg, 0.0, 360.0, def.trigPhaseDeg);
    c.trigVel = clampFinite(c.trigVel, 0.0, 1.0, def.trigVel);
    c.harmAmount = clampFinite(c.harmAmount, 0.0, 1.0, def.harmAmount);
    c.harmBalance = clampFinite(c.harmBalance, -1.0, 1.0, def.harmBalance);
    c.harmMax = std::clamp(c.harmMax, 3, 5);
    c.hybDivide = clampFinite(c.hybDivide, 0.0, 1.0, def.hybDivide);
    c.hybHarmonic = clampFinite(c.hybHarmonic, 0.0, 1.0, def.hybHarmonic);
    return c;
}
} // namespace subgen::engines
