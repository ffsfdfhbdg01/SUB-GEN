// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "core/Quality.h"
#include "engines/Divide.h"
#include "engines/Engine.h"
#include "engines/Harmonic.h"
#include "engines/Hybrid.h"
#include "engines/Track.h"
#include "engines/Trigger.h"

namespace subgen::engines
{
enum class EngineType
{
    Divide,
    Track,
    Trigger,
    Harmonic,
    Hybrid
};

// The engines behind one source (SPEC §6.1): all prepared with the same lookahead E = engineLookaheadSamples(q, fs),
// which is the host's latency whatever the engine. A switch (the `engine` parameter) warms the incoming engine up
// with the last 200 ms of the source, then crossfades over 30 ms at equal power (cos / sin); a switch asked during
// a crossfade waits for its end. Only the sounding engines run. Blocks longer than prepare()'s maxBlock are split.
// prepare() allocates; the other calls do not.
class EngineHost
{
public:
    void prepare(double fs, int maxBlock, Quality quality);
    void reset(); // every engine's state, the warm-up ring and a crossfade under way
    void setEngine(EngineType type) { target = type; }
    void setControl(const EngineControl& control);
    void process(std::span<const double> source, std::span<const double> key, std::span<double> out);

    EngineType engine() const { return target; }
    double subFrequencyHz() const { return reported().subFrequencyHz(); }
    double confidence() const { return reported().confidence(); }
    bool gateOpen() const { return reported().gateOpen(); }
    int latencySamples() const { return lookahead; }

private:
    Engine& engineFor(EngineType type);
    const Engine& reported() const; // the engine the host is on or moving to
    void beginSwitch();
    void processChunk(std::span<const double> source, std::span<const double> key, std::span<double> out);

    Divide divide;
    Track track;
    Trigger trigger;
    Harmonic harmonic;
    Hybrid hybrid;

    double fs = 48000.0;
    int maxBlock = 512;
    int lookahead = 0;
    EngineControl control;
    EngineType active = EngineType::Divide;
    EngineType target = EngineType::Divide;
    EngineType incoming = EngineType::Divide;
    bool switching = false;
    int fadeLength = 1440;
    int fadePosition = 0;
    std::vector<double> scratch;
    std::vector<double> sourceRing; // the last 200 ms of the source, for the warm-up
    std::vector<double> keyRing;
    size_t ringPosition = 0;
    size_t ringFill = 0;
};
} // namespace subgen::engines
