// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "engines/Divide.h"
#include "engines/Engine.h"
#include "engines/Harmonic.h"

namespace subgen::engines
{
// HYBRID (SPEC §6.6): hyb_divide DIVIDE (Sub -1 / Sub -2 at their levels) + hyb_harmonic HARMONIC, on the same source
// band settings; the pitch, the confidence and its gate are DIVIDE's. prepare() allocates; process() does not.
class Hybrid final : public Engine
{
public:
    void prepare(double fs, int maxBlock, int lookahead) override;
    void reset() override;
    void setControl(const EngineControl& control) override;
    void process(std::span<const double> source, std::span<const double> key, std::span<double> out) override;
    double subFrequencyHz() const override { return divide.subFrequencyHz(); }
    double confidence() const override { return divide.confidence(); }
    bool gateOpen() const override { return divide.gateOpen(); }

private:
    Divide divide;
    Harmonic harmonic;
    EngineControl control;
    std::vector<double> scratch;
};
} // namespace subgen::engines
