// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace subgen::analysis
{
// The last seconds of the dry band and of the sub after chain A, for Auto Align (SPEC §8.B). push() on the audio
// thread is lock-free: it announces the slots it is about to overwrite (`begun`), stores the samples as relaxed atomics
// and then publishes the new count (`written`). snapshot() on another thread copies the newest run and drops the samples
// the writer may have overwritten during the copy, so every snapshot is one contiguous run of what was pushed.
// Allocates only in prepare.
class CaptureBuffer
{
public:
    void prepare(double fs, double seconds = 8.0)
    {
        const double rate = std::isfinite(fs) && fs > 0.0 ? std::min(fs, 768000.0) : 48000.0; // a bounded ring
        capacity = std::max<size_t>(1, static_cast<size_t>(std::clamp(seconds, 0.001, 60.0) * rate));
        dry = std::make_unique<std::atomic<double>[]>(capacity);
        sub = std::make_unique<std::atomic<double>[]>(capacity);
        begun.store(0, std::memory_order_relaxed);
        written.store(0, std::memory_order_release);
    }

    void push(std::span<const double> dryBand, std::span<const double> subAfterA)
    {
        if (capacity == 0)
            return;
        const size_t n = std::min(dryBand.size(), subAfterA.size());
        const std::uint64_t w = written.load(std::memory_order_relaxed);
        begun.store(w + n, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (size_t i = 0; i < n; ++i)
        {
            const auto slot = static_cast<size_t>((w + i) % capacity);
            dry[slot].store(dryBand[i], std::memory_order_relaxed);
            sub[slot].store(subAfterA[i], std::memory_order_relaxed);
        }
        written.store(w + n, std::memory_order_release);
    }

    // The newest samples, oldest first; false while nothing is captured.
    bool snapshot(std::vector<double>& dryOut, std::vector<double>& subOut) const
    {
        dryOut.clear();
        subOut.clear();
        if (capacity == 0)
            return false;
        const std::uint64_t end = written.load(std::memory_order_acquire);
        const std::uint64_t count = std::min<std::uint64_t>(end, capacity);
        const std::uint64_t first = end - count;
        dryOut.resize(static_cast<size_t>(count));
        subOut.resize(static_cast<size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i)
        {
            const auto slot = static_cast<size_t>((first + i) % capacity);
            dryOut[static_cast<size_t>(i)] = dry[slot].load(std::memory_order_relaxed);
            subOut[static_cast<size_t>(i)] = sub[slot].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const std::uint64_t announced = begun.load(std::memory_order_relaxed);
        const std::uint64_t safeFrom = announced > capacity ? announced - capacity : 0; // older ones may be overwritten
        if (safeFrom > first)
        {
            const auto drop = static_cast<std::ptrdiff_t>(std::min<std::uint64_t>(safeFrom - first, count));
            dryOut.erase(dryOut.begin(), dryOut.begin() + drop);
            subOut.erase(subOut.begin(), subOut.begin() + drop);
        }
        return !dryOut.empty();
    }

private:
    size_t capacity = 0;
    std::unique_ptr<std::atomic<double>[]> dry;
    std::unique_ptr<std::atomic<double>[]> sub;
    std::atomic<std::uint64_t> begun{0};
    std::atomic<std::uint64_t> written{0};
};
} // namespace subgen::analysis
