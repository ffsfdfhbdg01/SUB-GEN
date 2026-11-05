// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

namespace subgen::test
{
// Measurements with the semantics of the prototype's util.py (and drive.nonharmonic_db).

struct PeakInfo
{
    double freqHz;
    double amplitude; // linear peak amplitude
};

double rms(std::span<const double> x);

// 20 log10(rms(ref - test) / rms(ref)); an exact null gives -400.
double nullDb(std::span<const double> ref, std::span<const double> test);

// Strongest spectral peak in [fLo, fHi]: periodic Blackman-Harris window, zero padding to a power of two of at least
// pad x the length, parabolic interpolation of the log magnitude.
PeakInfo peakFreq(std::span<const double> x, double fs, double fLo, double fHi, int pad = 16);

// Mean frequency between the first and the last rising zero crossing (0 with fewer than two).
double zcFreq(std::span<const double> x, double fs);

// Peak amplitudes at k f0 (k = 1..n), each searched within +-3 % of k f0; 0 from the first one near Nyquist on.
std::vector<double> harmonicAmps(std::span<const double> x, double fs, double f0, int n = 10);

double thdDb(std::span<const double> x, double fs, double f0, int n = 10);

// RMS level (dBFS) of the part of x between f1 and f2 (rectangular DFT, Parseval); any length.
double bandDb(std::span<const double> x, double fs, double f1, double f2);

// Power of the components that are not harmonics of f0 over the total power, in 20 Hz ... fMax (dB); x must hold
// whole cycles of f0 (T-ALIAS-01).
double nonharmonicDb(std::span<const double> x, double fs, double f0, double fMax);
} // namespace subgen::test
