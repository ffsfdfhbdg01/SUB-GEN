// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <span>
#include <vector>

#include <juce_core/juce_core.h>

namespace subgen::plugin
{
// The audio the editor draws (SPEC_IO_UI §6): four lanes of float written by the audio thread into one single-producer
// single-consumer FIFO, read by the message thread. Dry: S1 (the source stream); Sub: stream 1's sub after the
// limiter; Sum: the mean of the Front outputs (all outputs when there is none); OutPeak: the largest |sample| over the
// outputs. Written only while an editor is open, so a closed editor costs nothing; a full FIFO drops the newest
// samples. The sub chain's gain reduction comes as two atomics. No allocation after construction.
class UiFeed
{
public:
    enum Lane
    {
        Dry,
        Sub,
        Sum,
        OutPeak,
        kLanes
    };
    static constexpr int kCapacity = 1 << 15;

    UiFeed();

    void setEditorOpen(bool open); // message thread; opening empties the FIFO
    bool editorOpen() const { return open.load(std::memory_order_acquire); }

    // audio thread: n samples of each lane (same length)
    void push(std::span<const double> dry, std::span<const double> sub, std::span<const double> sum,
              std::span<const double> outPeak);
    void setGainReduction(double compDb, double limiterDb); // dB of reduction, >= 0

    // message thread: up to `max` samples of each lane into dst[lane]; returns how many
    int read(const std::array<float*, kLanes>& dst, int max);
    float compGainReductionDb() const { return compGr.load(std::memory_order_relaxed); }
    float limiterGainReductionDb() const { return limGr.load(std::memory_order_relaxed); }

private:
    juce::AbstractFifo fifo{kCapacity};
    std::array<std::vector<float>, kLanes> lanes;
    std::atomic<bool> open{false};
    std::atomic<float> compGr{0.0f}, limGr{0.0f};
};
} // namespace subgen::plugin
