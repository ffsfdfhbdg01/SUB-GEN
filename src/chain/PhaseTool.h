// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <span>

#include "dsp/Smoother.h"

namespace subgen::chain
{
// The all-pass Hilbert pair (SPEC §7.7, P2-07): chains A and B of four first-order sections (a + z^-1)/(1 + a z^-1),
// a = (g - 1)/(g + 1), g = p/(2 fs), with the prototype's minimax poles (hilbert.PAIR); B leads A by 90 degrees
// within 0.133 degrees over 12-600 Hz at any sample rate.
class HilbertPair
{
public:
    void prepare(double fs);
    void reset();
    void process(double x, double& a, double& b);
    double groupDelayMsA(double fHz) const;
    const std::array<double, 4>& coefficientsA() const { return coeffA; }

private:
    double fs = 48000.0;
    std::array<double, 4> coeffA{};
    std::array<double, 4> coeffB{};
    std::array<double, 4> stateA{};
    std::array<double, 4> stateB{};
};

struct PhaseParams
{
    bool invert = false;      // sub_pol
    double rotationDeg = 0.0; // sub_rot, -180...+180
};

// The sub path's polarity and phase rotation (SPEC §7.7): y = cos(theta) A + sin(theta) B, so theta = 0 is chain A, not
// the identity (its group delay belongs to the sub path's, which align_ms compensates); theta moves linearly over
// 20 ms (L20). The polarity crossfades over 5 ms (raised cosine through 0) and is then an exact -1. chainA() runs a copy
// of chain A from rest, for Auto Align (SPEC §8.B). The first parameters after prepare/reset start without ramps. No
// allocation.
class PhaseTool
{
public:
    void prepare(double fs);
    void reset();
    void setParams(const PhaseParams& params);
    void process(std::span<double> block);
    void chainA(std::span<const double> in, std::span<double> out) const;
    double groupDelayMs(double fHz) const { return pair.groupDelayMsA(fHz); }

private:
    double fs = 48000.0;
    bool fresh = true;
    HilbertPair pair;
    dsp::LinearSmoother rotation; // degrees
    dsp::LinearSmoother polarity; // 0: as it is, 1: inverted
    double madeDeg = 0.0;         // the angle cosT and sinT were made for
    double cosT = 1.0;
    double sinT = 0.0;
};
} // namespace subgen::chain
