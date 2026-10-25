// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/PartitionedConvolver.h"

#include <algorithm>

namespace subgen::dsp
{
namespace
{
int log2Of(int n)
{
    int e = 0;
    while ((1 << e) < n)
        ++e;
    return e;
}
} // namespace

void PartitionedConvolver::prepare(int maxKernel, int partitionSize)
{
    partition = 1 << log2Of(std::max(partitionSize, 1)); // a power of two
    const size_t p = static_cast<size_t>(partition);
    maxParts = std::max(1, (std::max(maxKernel, 1) + partition - 1) / partition);
    fft.emplace(log2Of(2 * partition));
    scratch.assign(2 * p, 0.0);
    previous.assign(p, 0.0);
    current.assign(p, 0.0);
    history.assign(static_cast<size_t>(maxParts) * (p + 1), {});
    accumulator.assign(p + 1, {});
    for (auto& lane : lanes)
    {
        lane.kernel = nullptr;
        lane.pending.assign(p, 0.0);
    }
    own = makeKernel();
    reset();
}

void PartitionedConvolver::reset()
{
    std::fill(previous.begin(), previous.end(), 0.0);
    std::fill(current.begin(), current.end(), 0.0);
    std::fill(history.begin(), history.end(), std::complex<double>{});
    for (auto& lane : lanes)
        std::fill(lane.pending.begin(), lane.pending.end(), 0.0);
    head = 0;
    fill = 0;
}

PartitionedConvolver::Kernel PartitionedConvolver::makeKernel() const
{
    Kernel k;
    k.spectra.assign(static_cast<size_t>(maxParts) * static_cast<size_t>(partition + 1), {});
    k.parts = 0;
    return k;
}

void PartitionedConvolver::transform(std::span<const double> taps, Kernel& kernel, RealFft& f,
                                     std::span<double> work) const
{
    const size_t p = static_cast<size_t>(partition);
    const size_t n = std::min(taps.size(), static_cast<size_t>(maxParts) * p);
    kernel.parts = std::max(1, static_cast<int>((n + p - 1) / p));
    for (size_t k = 0; k < static_cast<size_t>(kernel.parts); ++k)
    {
        std::fill(work.begin(), work.begin() + static_cast<std::ptrdiff_t>(2 * p), 0.0);
        const size_t from = k * p;
        if (from < n)
            std::copy_n(taps.begin() + static_cast<std::ptrdiff_t>(from), std::min(p, n - from), work.begin());
        f.forward(work.data(), &kernel.spectra[k * (p + 1)]);
    }
}

void PartitionedConvolver::setKernels(const Kernel* a, const Kernel* b)
{
    const std::array<const Kernel*, 2> wanted{a, b};
    for (size_t i = 0; i < lanes.size(); ++i)
        if (lanes[i].kernel != wanted[i])
        {
            lanes[i].kernel = wanted[i];
            convolve(lanes[i]);
        }
}

void PartitionedConvolver::setKernel(std::span<const double> taps)
{
    transform(taps, own, *fft, scratch);
    lanes[0].kernel = nullptr; // the spectra changed under the same pointer
    setKernels(&own, lanes[1].kernel);
}

void PartitionedConvolver::convolve(Lane& lane)
{
    const size_t p = static_cast<size_t>(partition);
    if (lane.kernel == nullptr)
    {
        std::fill(lane.pending.begin(), lane.pending.end(), 0.0);
        return;
    }
    std::fill(accumulator.begin(), accumulator.end(), std::complex<double>{});
    const int parts = std::min(lane.kernel->parts, maxParts);
    for (int k = 0; k < parts; ++k)
    {
        const size_t slot = static_cast<size_t>((head - k + maxParts) % maxParts);
        const auto* x = &history[slot * (p + 1)];
        const auto* h = &lane.kernel->spectra[static_cast<size_t>(k) * (p + 1)];
        for (size_t bin = 0; bin <= p; ++bin)
            accumulator[bin] += x[bin] * h[bin];
    }
    fft->inverse(accumulator.data(), scratch.data());
    std::copy(scratch.begin() + static_cast<std::ptrdiff_t>(p), scratch.end(), lane.pending.begin()); // overlap-save
}

void PartitionedConvolver::process(std::span<const double> in, std::span<double> outA, std::span<double> outB)
{
    const size_t p = static_cast<size_t>(partition);
    const bool second = lanes[1].kernel != nullptr;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double x = in[i];
        outA[i] = lanes[0].pending[fill];
        if (second)
            outB[i] = lanes[1].pending[fill];
        current[fill] = x;
        if (++fill == p)
        {
            fill = 0;
            std::copy(previous.begin(), previous.end(), scratch.begin());
            std::copy(current.begin(), current.end(), scratch.begin() + static_cast<std::ptrdiff_t>(p));
            head = (head + 1) % maxParts;
            fft->forward(scratch.data(), &history[static_cast<size_t>(head) * (p + 1)]);
            std::swap(previous, current);
            for (auto& lane : lanes)
                convolve(lane);
        }
    }
}
} // namespace subgen::dsp
