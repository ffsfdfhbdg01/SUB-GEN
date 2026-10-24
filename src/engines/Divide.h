// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "dsp/Envelope.h"
#include "dsp/Svf.h"
#include "engines/Activity.h"
#include "engines/ConfidenceGate.h"
#include "engines/Engine.h"
#include "engines/NoteTracker.h"
#include "engines/SourceBand.h"

namespace subgen::engines
{
// One output sample's values on the delayed axis (for tests).
struct DivideProbe
{
    double y1 = 0.0;           // x_b times the flip-flop sign s1 (Sub -1, native)
    double y2 = 0.0;           // y1 times s2 (Sub -2, native)
    double fEst = 0.0;         // held estimate of the source frequency (0 when no value is held)
    double conf = 0.0;         // held confidence (the prototype's divide().conf)
    bool provisional = false;  // the confidence is the provisional value of a note start
    bool reset = false;        // a polarity reset happened at this sample
    double cutoffTarget = 0.0; // Sub -1 smoothing cutoff before the 5 ms log slew
    double smoothed = 0.0;     // Sub -1 after the smoothing low-pass, before the gate (Follow)
    double constantScale = 1.0; // Constant: 10^(-12/20) / sliding peak of |x_b| (1 in Follow)
};

namespace divide_detail
{
enum class SignOp : std::uint8_t
{
    Flip1,
    Flip2,
    Switch,
    Ramp
};

// The note events of one sample, in the prototype's order: a start detected inside the sample (Live) comes before
// the crossing; an end and a start whose reset point is the sample come after it, the end first.
struct SampleEvents
{
    bool startBefore = false;
    bool end = false;
    bool startAfter = false;
    bool ramp = false;       // the start is Live's 0.5 ms ramp, else a switch at a zero crossing
    double startTime = 0.0;  // the start's processing time in the prototype
};

// The prototype's divide() (proto/subgen_proto/divide.py) as a causal state machine over samples. Plain data, so the
// current-axis copy can be re-derived from the delayed one by copying it.
struct Core
{
    // configuration
    double fs = 48000.0;
    double minGap = 1920.0; // MIN_GAP_S fs: the hold of a value while no period is known

    // processing position and arming
    std::int64_t next = 0; // the next sample to process
    double prevX = 0.0;
    std::int64_t csNeg = 0; // samples below -theta so far
    std::int64_t csPos = 0;
    std::int64_t baseNeg = 0; // csNeg at the last fired rising crossing (or before the last reset)
    std::int64_t basePos = 0;

    // flip-flops and note state
    int s1 = 1;
    bool pending = false;
    bool running = false;
    bool provisional = false;
    bool endPending = false;
    double lastStartTime = -1e300; // processing time of the last applied start (prototype order of ends and starts)

    // period history of the current note
    int crossings = 0; // fired rising crossings since the reset
    double lastCrossing = 0.0;
    std::array<double, 3> periods{};
    int periodCount = 0;
    std::array<double, 5> residuals{};
    int residualCount = 0;
    bool hasSmoothedF = false;
    double smoothedF = 0.0;
    double lastPeriod = 0.0;

    // the held values (the prototype's store), valid at heldIndex <= sample < heldEnd
    std::int64_t heldIndex = -1;
    std::int64_t heldEnd = -1;
    double heldF = 0.0;
    double heldConf = 0.0;
    bool heldProvisional = false;

    int resets = 0;
    bool resetNow = false; // a reset happened at the last processed sample

    // F-1: consecutive confidence values >= gateHi on this core's time line (divide.confirmations): every change of
    // (conf, provisional) is a value; a provisional one counts; a value below gateHi resets the count
    double gateHi = 0.5;
    int confirmations = 0;
    bool hasOutput = false;
    double outConf = 0.0;
    bool outProvisional = false;

    void configure(double sampleRate);
    void clearState();

    // Processes sample `next`; writes its sign operations in order into ops and returns their count.
    int step(double x, double theta, const SampleEvents& ev, std::array<SignOp, 4>& ops);

    // Skips to sample `target` over samples without a zero crossing or an event: only the arming counts, the previous
    // input and the expiry of the held value change there (the replay of core B).
    void jump(std::int64_t target, double xBefore, std::int64_t negThrough, std::int64_t posThrough);

    std::int64_t last() const { return next - 1; }
    double fEst() const { return held() ? heldF : 0.0; }
    double conf() const { return held() ? heldConf : 0.0; }
    bool isProvisional() const { return held() && heldProvisional; }

private:
    bool held() const { return heldIndex <= last() && last() < heldEnd; }
    void countConfirmation();
    void store(std::int64_t index, double f, double c, double period, bool prov);
    void clear(std::int64_t k, SignOp kind, double confAfter, std::int64_t negBefore, std::int64_t posBefore,
               std::array<SignOp, 4>& ops, int& count);
    void start(std::int64_t k, bool ramp, double time, std::int64_t negBefore, std::int64_t posBefore,
               std::array<SignOp, 4>& ops, int& count);
    void risingFired(std::int64_t idx, double t);
};
} // namespace divide_detail

// DIVIDE (SPEC §6.2). The prototype computes offline; in Mix and Print it resets a note's polarity at a zero
// crossing up to the lookahead E before the note is detected. Here (Phase 3 plan, Task 8):
//  - core A runs on the axis delayed by E and is the prototype's state machine; a start is applied at its reset
//    point, which is never behind A's position when the start becomes known, so A's y1, y2, f_est, conf equal
//    divide.divide();
//  - core B runs on the current input and gives the confidence and its confirmation count (F-1, ADR-030) as known
//    now, which the gate reads (E ahead of A): at every note start B is re-derived as a copy of A advanced over the
//    last E samples, with a full step only at their zero crossings and events (render.divide_sub_causal);
//  - the flip-flop signs, the tracking low-pass (shape as a cutoff shift, 5 ms log slew), Constant and the sub levels
//    work on the delayed axis. In Live (E = 0) A and B are the same core.
// prepare() allocates; process() does not.
class Divide final : public Engine
{
public:
    void prepare(double fs, int maxBlock, int lookahead) override;
    void reset() override;
    void setControl(const EngineControl& control) override;
    void process(std::span<const double> source, std::span<const double> key, std::span<double> out) override;
    double subFrequencyHz() const override { return 0.5 * lastFEst; }
    double confidence() const override { return lastConfKnown; }
    bool gateOpen() const override { return gate.open(); }

    // for tests
    double lastPeriodSamples() const { return coreA.lastPeriod; }
    int resetCount() const { return coreA.resets; }
    const DivideProbe& probe() const { return lastProbe; }
    int replayCount() const { return replays; }           // re-derivations of core B (note starts in Mix and Print)
    int maxReplaySteps() const { return replayStepsMax; } // the most full core steps one re-derivation took

private:
    struct QueuedEvent
    {
        std::int64_t sample = 0;
        bool after = true; // after the crossing
        bool isEnd = false;
        bool ramp = false;
        double time = 0.0; // processing time
    };

    struct Signs
    {
        double cur1 = 1.0;
        double cur2 = 1.0;
        std::int64_t rampStart = 0;
        std::int64_t rampEnd = -1;
        double from1 = 1.0;
        double from2 = 1.0;
        std::array<divide_detail::SignOp, 16> deferred{};
        int deferredCount = 0;
    };

    struct Smoother // the tracking low-pass of one sub: two TPT sections with a per-sample cutoff
    {
        bool initialised = false;
        double slewState = 0.0;
        dsp::SvfSection a, b;
    };

    double processSample(double source);
    bool advanceCurrentAxis(std::int64_t n, double xb); // true when a note start became known
    void insertEvent(const QueuedEvent& e);
    divide_detail::SampleEvents eventsFor(std::int64_t sample, bool consume);
    void scheduleStart(std::int64_t n, double detect, double begin);
    void applySignOp(divide_detail::SignOp op, std::int64_t at);
    void currentSigns(std::int64_t at, double& s1, double& s2);
    double smooth(Smoother& s, double y, double cutoff);
    double ringX(std::int64_t i) const { return ringXb[static_cast<size_t>(i) % ringXb.size()]; }
    double ringT(std::int64_t i) const { return ringTheta[static_cast<size_t>(i) % ringTheta.size()]; }
    void rederiveCoreB(std::int64_t cur);

    double fs = 48000.0;
    int look = 0;
    int rampLength = 24;
    double slewCoeff = 0.0;
    EngineControl control;
    double fInit = 120.0;

    SourceBand band;
    Activity activity;
    NoteTracker notes;
    dsp::PeakFollower ePeak;
    ConfidenceGate gate;
    dsp::SlidingMax constantPeak;

    std::vector<double> ringXb;
    std::vector<double> ringTheta;
    std::vector<std::int64_t> ringNeg; // arming counts through each sample, as the cores count them
    std::vector<std::int64_t> ringPos;
    std::vector<std::int64_t> crossings; // recent zero-crossing samples (a ring), for the sparse replay of core B
    size_t crossingHead = 0;
    size_t crossingCount = 0;
    std::int64_t cumNeg = 0;
    std::int64_t cumPos = 0;
    std::array<QueuedEvent, 16> queue{};
    int queueCount = 0;

    divide_detail::Core coreA;
    divide_detail::Core coreB;
    Signs signs;
    Smoother smooth1, smooth2;
    double fHeld = 0.0;
    double fConstant = 0.0; // the estimate the Constant window uses (0: none yet in this note)

    std::int64_t n = 0; // samples processed on the current axis
    double lastFEst = 0.0;
    double lastConfKnown = 0.0;
    DivideProbe lastProbe;
    int replays = 0;
    int replayStepsMax = 0;
};
} // namespace subgen::engines
