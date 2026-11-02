// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "chain/DriveShapes.h"

namespace subgen::chain
{
// Auto Gain of the Drive (SPEC §7.3, ADR-031): the powers |c_k|^2 of harmonics 1..64 of one period of the blended
// shaper output y_alpha = (1 - alpha) r + alpha f_b(g r), r a unit sine (2048 points), on a grid of drive 0...100 % (1 %
// steps: 2 % missed alpha's kink at 5 % by 0.5 dB) x balance -100...+100 (steps of 20) x the five modes. Computed once
// per process, at the first instance() call (Drive::prepare). compensationDb() interpolates the powers bilinearly and
// returns G_c = RMS(r) / RMS_AC with RMS_AC^2 = 1/2 sum_k |c_k|^2 over the harmonics below fs/2: the shaper only, the
// Tone is not compensated (F-8: with a Tone far below the harmonics the exact value ran to +55 dB). No allocation after
// the computation.
class AutoGainTable
{
public:
    static constexpr int kHarmonics = 64;

    static const AutoGainTable& instance();
    static int computations(); // how often the table was computed (for tests: 1)

    double compensationDb(DriveMode mode, double drivePct, double balance, double fSub1Hz, double fs) const;

private:
    AutoGainTable();
    const double* cell(int mode, int drive, int balance) const;

    std::vector<double> power; // [mode][drive][balance][k - 1]
};
} // namespace subgen::chain
