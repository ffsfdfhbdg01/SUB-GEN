// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugin/UiFeed.h"

#include <algorithm>

namespace subgen::plugin
{
UiFeed::UiFeed()
{
    for (auto& lane : lanes)
        lane.assign(static_cast<size_t>(kCapacity), 0.0f);
}

void UiFeed::setEditorOpen(bool isOpen)
{
    if (isOpen)
    {
        // the reader empties what a previous editor left; the writer only starts once `open` says so
        const int ready = fifo.getNumReady();
        fifo.finishedRead(ready);
    }
    open.store(isOpen, std::memory_order_release);
}

void UiFeed::push(std::span<const double> dry, std::span<const double> sub, std::span<const double> sum,
                  std::span<const double> outPeak)
{
    if (!editorOpen())
        return;
    const auto n = static_cast<int>(std::min({dry.size(), sub.size(), sum.size(), outPeak.size()}));
    const std::array<std::span<const double>, kLanes> src{dry, sub, sum, outPeak};
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(n, start1, size1, start2, size2);
    for (size_t l = 0; l < lanes.size(); ++l)
    {
        auto* lane = lanes[l].data();
        const auto& s = src[l];
        for (int i = 0; i < size1; ++i)
            lane[start1 + i] = static_cast<float>(s[static_cast<size_t>(i)]);
        for (int i = 0; i < size2; ++i)
            lane[start2 + i] = static_cast<float>(s[static_cast<size_t>(size1 + i)]);
    }
    fifo.finishedWrite(size1 + size2);
}

void UiFeed::setGainReduction(double compDb, double limiterDb)
{
    if (!editorOpen())
        return;
    compGr.store(static_cast<float>(std::max(0.0, compDb)), std::memory_order_relaxed);
    limGr.store(static_cast<float>(std::max(0.0, limiterDb)), std::memory_order_relaxed);
}

int UiFeed::read(const std::array<float*, kLanes>& dst, int max)
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead(max, start1, size1, start2, size2);
    for (size_t l = 0; l < lanes.size(); ++l)
    {
        const auto* lane = lanes[l].data();
        std::copy(lane + start1, lane + start1 + size1, dst[l]);
        std::copy(lane + start2, lane + start2 + size2, dst[l] + size1);
    }
    fifo.finishedRead(size1 + size2);
    return size1 + size2;
}
} // namespace subgen::plugin
