// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "dsp/Fft.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/Smoother.h"

namespace subgen::chain
{
struct SubFilterParams;

// The Linear Phase low-pass (SPEC §7.6, D5, ADR-020; Print only): a symmetric FIR of N_LIN taps whose amplitude is |H|
// of the minimum-phase low-pass the parameters ask for (sub_lp, or lfe_lp in LFE mode), run by a uniformly partitioned
// convolution. Latency (N_LIN - 1)/2 + 256 whatever the filter. A worker thread designs the kernels (requestDesign is
// lock-free) into four preallocated slots and hands each over by an atomic index; the new kernel replaces the sounding
// one with a 40 ms raised-cosine crossfade (the first one fades in from silence). In real time the swap is
// asynchronous, so automating the low-pass in Linear Phase is not sample-deterministic; an offline render
// (setNonRealtime) has no deadline and waits for each kernel, so it is. No allocation on the audio thread.
class LinearPhaseLp
{
public:
    LinearPhaseLp() = default;
    ~LinearPhaseLp();
    LinearPhaseLp(const LinearPhaseLp&) = delete;
    LinearPhaseLp& operator=(const LinearPhaseLp&) = delete;

    void prepare(double fs, int maxBlock); // allocates and (re)starts the worker
    void release();                        // stops the worker and frees the kernels (Live and Mix)
    void reset();                          // the convolution history; the kernels stay
    void requestDesign(const SubFilterParams& params);
    void setNonRealtime(bool offline) { nonRealtime.store(offline, std::memory_order_relaxed); }
    void process(std::span<double> block);
    int latencySamples() const { return latency; }
    double errorDb() const { return soundingError.load(std::memory_order_relaxed); } // the sounding kernel's (UI)
    // Off the audio thread (tests, offline rendering): waits until the kernel last asked for is handed over.
    bool waitUntilDesigned(int timeoutMs) const;

private:
    enum SlotState : int
    {
        Free,
        Writing,
        Ready,
        InUse
    };

    struct Slot
    {
        dsp::PartitionedConvolver::Kernel kernel;
        double errorDb = 0.0;
    };

    void stopWorker();
    void work();
    void design(std::uint64_t wanted);
    void processChunk(std::span<double> chunk);

    double fs = 48000.0;
    int taps = 0;
    int latency = 0;
    dsp::PartitionedConvolver convolver;
    std::array<Slot, 4> slots;
    std::array<std::atomic<int>, 4> state{};  // SlotState
    std::atomic<int> ready{-1};               // the slot handed over and not yet taken
    std::atomic<std::uint64_t> request{0};    // the low-pass asked for, packed (0: none)
    std::atomic<std::uint64_t> handedOver{0}; // the last request whose kernel was handed over
    std::atomic<double> soundingError{0.0};
    std::atomic<bool> nonRealtime{false};

    // the audio thread
    std::uint64_t lastRequest = 0;
    int sounding = -1; // the slot in lane A
    int incoming = -1; // the slot in lane B while it fades in
    dsp::LinearSmoother fade;
    std::vector<double> laneB;

    // the worker
    std::vector<double> workTaps;
    std::optional<dsp::RealFft> workFft;
    std::vector<double> workScratch;
    std::thread worker;
    std::atomic<bool> quit{false};
    std::mutex wakeMutex;
    std::condition_variable wake;
};
} // namespace subgen::chain
