// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/LinearPhaseLp.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <numbers>

#include "chain/SubFilters.h"
#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "dsp/FirDesign.h"

namespace subgen::chain
{
namespace
{
constexpr int kPartition = 256;
constexpr int kFftOrder = 9; // 2 x 256 points
constexpr double kFadeMs = 40.0;
constexpr int kPollMs = 10;          // the worker's idle check: the audio thread never wakes it in real time
constexpr int kOfflineWaitMs = 10000; // the longest an offline render waits for a kernel

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

// family, order and the corner (as a float) in one word, so the worker never reads half of a request
std::uint64_t pack(const LowPassDesign& d)
{
    return (std::uint64_t{1} << 63) | (static_cast<std::uint64_t>(d.family) << 40) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(d.order)) << 32) |
           std::bit_cast<std::uint32_t>(static_cast<float>(d.fcHz));
}

LowPassDesign unpack(std::uint64_t v)
{
    LowPassDesign d;
    d.family = static_cast<dsp::FilterFamily>((v >> 40) & 0xffu);
    d.order = static_cast<int>((v >> 32) & 0xffu);
    d.fcHz = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(v & 0xffffffffu)));
    return d;
}
} // namespace

LinearPhaseLp::~LinearPhaseLp() { stopWorker(); }

void LinearPhaseLp::prepare(double sampleRate, int maxBlock)
{
    stopWorker();
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    taps = dsp::linearPhaseTaps(fs);
    convolver.prepare(taps, kPartition);
    latency = (taps - 1) / 2 + convolver.latencySamples();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        slots[i].kernel = convolver.makeKernel();
        slots[i].errorDb = 0.0;
        state[i].store(Free, std::memory_order_relaxed);
    }
    ready.store(-1, std::memory_order_relaxed);
    request.store(0, std::memory_order_relaxed);
    handedOver.store(0, std::memory_order_relaxed);
    soundingError.store(0.0, std::memory_order_relaxed);
    lastRequest = 0;
    sounding = incoming = -1;
    convolver.setKernels(nullptr, nullptr);
    fade.reset(fs, kFadeMs, 1.0);
    laneB.assign(static_cast<size_t>(std::max(maxBlock, 1)), 0.0);
    workTaps.assign(static_cast<size_t>(taps), 0.0);
    workFft.emplace(kFftOrder);
    workScratch.assign(2 * kPartition, 0.0);
    quit.store(false, std::memory_order_release);
    worker = std::thread([this] { work(); });
}

void LinearPhaseLp::release()
{
    stopWorker();
    convolver = dsp::PartitionedConvolver{};
    for (auto& s : slots)
        s.kernel = {};
    laneB.clear();
    workTaps.clear();
    workFft.reset();
    workScratch.clear();
    sounding = incoming = -1;
    latency = 0;
}

void LinearPhaseLp::reset() { convolver.reset(); }

void LinearPhaseLp::stopWorker()
{
    if (!worker.joinable())
        return;
    {
        std::lock_guard lock(wakeMutex);
        quit.store(true, std::memory_order_release);
    }
    wake.notify_all();
    worker.join();
}

void LinearPhaseLp::requestDesign(const SubFilterParams& params)
{
    const std::uint64_t packed = pack(lowPassOf(params, fs));
    if (packed == lastRequest)
        return;
    lastRequest = packed;
    request.store(packed, std::memory_order_release);
    if (nonRealtime.load(std::memory_order_relaxed)) // offline there is no deadline: wake the worker at once
    {
        {
            std::lock_guard lock(wakeMutex);
        }
        wake.notify_one();
    }
}

void LinearPhaseLp::work()
{
    std::uint64_t done = 0;
    while (!quit.load(std::memory_order_acquire))
    {
        const std::uint64_t wanted = request.load(std::memory_order_acquire);
        if (wanted != 0 && wanted != done)
        {
            design(wanted);
            done = wanted;
            continue;
        }
        std::unique_lock lock(wakeMutex); // in real time the audio thread never takes this lock: polled
        wake.wait_for(lock, std::chrono::milliseconds(kPollMs), [&] {
            return quit.load(std::memory_order_acquire) || request.load(std::memory_order_acquire) != done;
        });
    }
}

void LinearPhaseLp::design(std::uint64_t wanted)
{
    const LowPassDesign lp = unpack(wanted);
    dsp::FilterCascade prototype;
    prototype.set(dsp::designFilter(lp.family, dsp::FilterKind::LowPass, lp.order, lp.fcHz, fs));
    const auto mag = [&](double f) { return std::max(std::abs(prototype.response(f, fs)), 1e-15); };
    dsp::designLinearPhaseKernel(mag, fs, workTaps);
    const double error = dsp::kernelErrorDb(workTaps, mag, fs);
    // a free slot: at most two are in use and one is handed over, so one of the four is free (or soon is)
    size_t s = slots.size();
    while (s == slots.size())
    {
        for (size_t i = 0; i < slots.size() && s == slots.size(); ++i)
        {
            int expected = Free;
            if (state[i].compare_exchange_strong(expected, Writing, std::memory_order_acq_rel))
                s = i;
        }
        if (s == slots.size())
        {
            if (quit.load(std::memory_order_acquire))
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    convolver.transform(workTaps, slots[s].kernel, *workFft, workScratch);
    slots[s].errorDb = error;
    state[s].store(Ready, std::memory_order_release);
    const int superseded = ready.exchange(static_cast<int>(s), std::memory_order_acq_rel);
    if (superseded >= 0) // not taken: free again
        state[static_cast<size_t>(superseded)].store(Free, std::memory_order_release);
    handedOver.store(wanted, std::memory_order_release);
}

bool LinearPhaseLp::waitUntilDesigned(int timeoutMs) const
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (handedOver.load(std::memory_order_acquire) != request.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() > until)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void LinearPhaseLp::process(std::span<double> block)
{
    if (laneB.empty()) // not prepared: no kernel
    {
        std::fill(block.begin(), block.end(), 0.0);
        return;
    }
    if (nonRealtime.load(std::memory_order_relaxed))
        (void)waitUntilDesigned(kOfflineWaitMs);
    for (size_t done = 0; done < block.size();)
    {
        const size_t n = std::min(block.size() - done, laneB.size());
        processChunk(block.subspan(done, n));
        done += n;
    }
}

void LinearPhaseLp::processChunk(std::span<double> chunk)
{
    if (incoming < 0) // one crossfade at a time; a kernel handed over meanwhile waits
    {
        const int s = ready.exchange(-1, std::memory_order_acq_rel);
        if (s >= 0)
        {
            state[static_cast<size_t>(s)].store(InUse, std::memory_order_release);
            incoming = s;
            convolver.setKernels(sounding >= 0 ? &slots[static_cast<size_t>(sounding)].kernel : nullptr,
                                 &slots[static_cast<size_t>(s)].kernel);
            fade.reset(fs, kFadeMs, 0.0);
            fade.setTarget(1.0);
        }
    }
    if (incoming < 0)
    {
        convolver.process(chunk, chunk, {});
        return;
    }
    const auto b = std::span<double>(laneB).subspan(0, chunk.size());
    convolver.process(chunk, chunk, b);
    for (size_t i = 0; i < chunk.size(); ++i)
    {
        const double w = sCurve(fade.next());
        chunk[i] = w * b[i] + (1.0 - w) * chunk[i];
    }
    if (!fade.isSmoothing()) // the incoming kernel sounds alone
    {
        if (sounding >= 0)
            state[static_cast<size_t>(sounding)].store(Free, std::memory_order_release);
        sounding = incoming;
        incoming = -1;
        soundingError.store(slots[static_cast<size_t>(sounding)].errorDb, std::memory_order_relaxed);
        convolver.setKernels(&slots[static_cast<size_t>(sounding)].kernel, nullptr);
    }
}
} // namespace subgen::chain
