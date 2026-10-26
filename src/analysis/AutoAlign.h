// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <span>

namespace subgen::analysis
{
enum class AlignObjective // align_obj
{
    Energy,
    Headroom
};

struct AlignResult
{
    bool clearOptimum = false; // confidence >= 0.5 dB
    int polarity = 1;
    double rotationDeg = 0.0;  // |theta| <= 90
    double gainDb = 0.0;       // against the untouched sub (polarity +1, 0 degrees)
    double confidenceDb = 0.0; // the best against the median of all rotations
};

// Auto Align (SPEC §8.B, P2-07, ADR-019; the prototype's align.auto_align): z = d + Re{a e^(j psi)} over psi on a grid
// of stepDeg (24 rotations by default), a = the analytic signal of the sub after chain A (an FFT zero-padded to a power
// of two, as scipy.signal.hilbert at that length). Energy: the most energy of z in [max(rangeLo/2, 20 Hz), rangeHi]
// (Hann window); Headroom: the smallest peak |z|. Candidates within 1e-3 dB of the best tie and the one nearest the
// identity wins; the result is canonical: |theta| <= 90 and a polarity. Silence or too little data gives the identity.
// Allocates: it runs off the audio thread.
AlignResult autoAlign(std::span<const double> dryBand, std::span<const double> subAfterA, double fs, double rangeLoHz,
                      double rangeHiHz, AlignObjective objective, double stepDeg = 15.0);
} // namespace subgen::analysis
