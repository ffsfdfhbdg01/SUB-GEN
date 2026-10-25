// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <complex>
#include <optional>
#include <span>
#include <vector>

#include "dsp/Fft.h"

namespace subgen::dsp
{
// Uniformly partitioned overlap-save convolution: partitions of P samples, FFTs of 2P points, latency P. The input
// partitions' spectra (the frequency-domain delay line) are shared by two output lanes, so a crossfade between two
// kernels costs the second lane's products and one inverse FFT per partition. Allocates only in prepare and makeKernel.
class PartitionedConvolver
{
public:
    // A kernel's partition spectra: parts x (P + 1) bins.
    struct Kernel
    {
        std::vector<std::complex<double>> spectra;
        int parts = 0;
    };

    void prepare(int maxKernel, int partition = 256);
    void reset(); // the input history and the pending output; the lanes keep their kernels

    Kernel makeKernel() const; // storage for maxKernel taps
    // The partition spectra of taps (at most maxKernel) into kernel, with the caller's FFT of 2P points and scratch of
    // 2P samples. Const and allocation-free: a worker thread can make kernels while process() runs.
    void transform(std::span<const double> taps, Kernel& kernel, RealFft& fft, std::span<double> scratch) const;

    // The lanes' kernels (nullptr: silence). A lane whose kernel changes recomputes its pending partition from the
    // history at once, so the new kernel is exact from the next sample. Allocation-free.
    void setKernels(const Kernel* a, const Kernel* b);
    // in -> outA through lane A and, when lane B has a kernel, -> outB through lane B; in may alias outA.
    void process(std::span<const double> in, std::span<double> outA, std::span<double> outB);

    // the own kernel in lane A, for a plain filter: setKernel is allocation-free after prepare
    void setKernel(std::span<const double> taps);
    void process(std::span<double> block) { process(block, block, {}); }

    int latencySamples() const { return partition; }
    int partitionSize() const { return partition; }

private:
    struct Lane
    {
        const Kernel* kernel = nullptr;
        std::vector<double> pending; // the outputs of the last full partition (P)
    };

    void convolve(Lane& lane);

    int partition = 256;
    int maxParts = 1;
    std::optional<RealFft> fft;
    std::vector<double> scratch;                    // 2P
    std::vector<double> previous;                   // the last full partition's input
    std::vector<double> current;                    // the partition being filled
    std::vector<std::complex<double>> history;      // maxParts x (P + 1), circular; the newest at `head`
    std::vector<std::complex<double>> accumulator;  // P + 1
    int head = 0;
    size_t fill = 0;
    std::array<Lane, 2> lanes;
    Kernel own;
};
} // namespace subgen::dsp
