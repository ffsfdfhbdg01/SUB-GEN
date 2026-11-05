// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace subgen::ui
{
// The analyser's spectrum (SPEC_IO_UI §6): FFT 16384, Hann, 75 % overlap, 1/6-octave smoothing, read at kPoints
// log-spaced frequencies over 10-500 Hz. A full-scale sine reads 0 dB. Message thread; allocates only in prepare().
class Spectrum
{
public:
    static constexpr int kOrder = 14;
    static constexpr int kSize = 1 << kOrder;
    static constexpr int kHop = kSize / 4;
    static constexpr int kPoints = 192;
    static constexpr float kFloorDb = -120.0f;

    void prepare(double sampleRate);
    void push(const float* x, int n);
    bool consumeUpdate(); // a new frame since the last call
    const std::array<float, kPoints>& levelsDb() const { return levels; }
    static double pointHz(int i); // 10 Hz ... 500 Hz

private:
    void analyse();

    double fs = 48000.0;
    juce::dsp::FFT fft{kOrder};
    std::vector<float> ring;   // the last kSize samples, the oldest at `pos`
    std::vector<float> window; // Hann
    std::vector<float> work;   // 2 * kSize
    std::vector<float> power;  // kSize / 2 + 1
    int pos = 0;
    int sinceHop = 0;
    int filled = 0;
    bool fresh = false;
    std::array<float, kPoints> levels{};
};
} // namespace subgen::ui
