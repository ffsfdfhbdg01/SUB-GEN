// SPDX-License-Identifier: AGPL-3.0-or-later
// SPK-6 (SPEC_BUILD_RELEASE §7, §12): the sub filters with the Linear Phase low-pass in Print, two channels (an upper
// bound for a stereo instance: the sub path itself is mono), 48 kHz, blocks of 128 in JUCE buffers; the median and the
// 99th percentile of the time per block over 1000 blocks, as a share of the block's duration. Task 29 checks the
// budget of the whole plug-in.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "chain/SubFilters.h"
#include "support/Signals.h"

using namespace subgen;

TEST_CASE("SPK-6 Linear Phase low-pass in Print: two channels, 48 kHz, 128-sample blocks", "[bench]")
{
    constexpr double kFs = 48000.0;
    constexpr int kBlock = 128;
    constexpr size_t kBlocks = 1000;
    constexpr size_t kWarmUp = 50; // the first kernel's 40 ms fade-in
    const auto source = test::signals::bass808(kFs);
    auto measure = [&](bool linearPhase) {
        std::array<chain::SubFilters, 2> channels;
        chain::SubFilterParams p;
        p.linearPhase = linearPhase;
        for (auto& f : channels)
        {
            f.prepare(kFs, kBlock, Quality::Print);
            f.setParams(p);
            REQUIRE(f.waitForLinearPhase(20000));
        }
        juce::AudioBuffer<double> buffer(2, kBlock);
        std::vector<double> seconds;
        seconds.reserve(kBlocks);
        size_t pos = 0;
        for (size_t b = 0; b < kBlocks + kWarmUp; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < kBlock; ++i)
                    buffer.setSample(c, i, source[(pos + static_cast<size_t>(i)) % source.size()]);
            pos += kBlock;
            const auto t0 = std::chrono::steady_clock::now();
            for (int c = 0; c < 2; ++c)
                channels[static_cast<size_t>(c)].process(std::span<double>(buffer.getWritePointer(c), kBlock));
            const auto t1 = std::chrono::steady_clock::now();
            if (b >= kWarmUp)
                seconds.push_back(std::chrono::duration<double>(t1 - t0).count());
        }
        std::sort(seconds.begin(), seconds.end());
        const double block = kBlock / kFs;
        return std::pair{100.0 * seconds[seconds.size() / 2] / block, 100.0 * seconds[seconds.size() * 99 / 100] / block};
    };
    const auto [minMedian, minP99] = measure(false);
    const auto [linMedian, linP99] = measure(true);
    std::printf("SPK-6 sub filters, 2 channels, 48 kHz, 128-sample blocks, %zu blocks:\n", kBlocks);
    std::printf("  Min Phase:    median %.3f %%, p99 %.3f %% of the block\n", minMedian, minP99);
    std::printf("  Linear Phase: median %.3f %%, p99 %.3f %% of the block\n", linMedian, linP99);
    CHECK(linP99 < 100.0); // real time; the budget is Task 29's
}
