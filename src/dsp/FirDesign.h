// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include <span>

namespace subgen::dsp
{
// N_LIN = 4096 * 2^ceil(log2(fs / 48 kHz)) + 1 (SPEC §7.6): 4097 taps at 44.1 / 48 kHz, 8193 at 88.2 / 96, 16385 at
// 176.4 / 192.
int linearPhaseTaps(double fs);

// A symmetric kernel of kernel.size() taps (odd, so the delay (N - 1)/2 is whole) whose AMPLITUDE follows mag(f), as
// the prototype's lfe.linear_phase_fir: the zero-phase response sampled on 4 * 2^ceil(log2 N) points, delayed by
// (N - 1)/2, windowed with Kaiser(beta) and symmetrised. Allocates (designs run off the audio thread).
void designLinearPhaseKernel(const std::function<double(double)>& mag, double fs, std::span<double> kernel,
                             double beta = 3.0);

// The largest |20 log10(|K(f)| / mag(f))| from 5 Hz up to the first frequency where mag is at or below -60 dB, on a
// 2^19-point FFT of the kernel (SPEC §7.6, T-FILT-06). Allocates.
double kernelErrorDb(std::span<const double> kernel, const std::function<double(double)>& mag, double fs);
} // namespace subgen::dsp
