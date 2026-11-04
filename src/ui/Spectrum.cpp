// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Spectrum.h"

#include <algorithm>
#include <cmath>

namespace subgen::ui
{
void Spectrum::prepare(double sampleRate)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    ring.assign(kSize, 0.0f);
    window.resize(kSize);
    for (int i = 0; i < kSize; ++i)
        window[static_cast<size_t>(i)] =
            0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(kSize));
    work.assign(2 * kSize, 0.0f);
    power.assign(kSize / 2 + 1, 0.0f);
    pos = sinceHop = filled = 0;
    fresh = false;
    levels.fill(kFloorDb);
}

double Spectrum::pointHz(int i) { return 10.0 * std::pow(50.0, static_cast<double>(i) / (kPoints - 1)); }

void Spectrum::push(const float* x, int n)
{
    if (ring.empty())
        prepare(fs);
    for (int i = 0; i < n; ++i)
    {
        ring[static_cast<size_t>(pos)] = std::isfinite(x[i]) ? x[i] : 0.0f;
        pos = (pos + 1) % kSize;
        filled = std::min(kSize, filled + 1);
        if (++sinceHop >= kHop && filled == kSize)
        {
            sinceHop = 0;
            analyse();
        }
    }
}

bool Spectrum::consumeUpdate()
{
    const bool was = fresh;
    fresh = false;
    return was;
}

void Spectrum::analyse()
{
    for (int i = 0; i < kSize; ++i)
        work[static_cast<size_t>(i)] = ring[static_cast<size_t>((pos + i) % kSize)] * window[static_cast<size_t>(i)];
    std::fill(work.begin() + kSize, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform(work.data(), true);
    // a sine of amplitude A peaks at A * N / 4 through a Hann window: that is 0 dB for A = 1
    const float norm = 4.0f / static_cast<float>(kSize);
    for (size_t k = 0; k < power.size(); ++k)
    {
        const float m = work[k] * norm;
        power[k] = m * m;
    }
    const double binHz = fs / kSize;
    const double sixth = std::pow(2.0, 1.0 / 12.0); // half of 1/6 octave each side
    auto powerAt = [&](double f) { // linear in power between bins
        const double b = std::clamp(f / binHz, 0.0, static_cast<double>(power.size() - 2));
        const auto k = static_cast<size_t>(b);
        const double t = b - static_cast<double>(k);
        return (1.0 - t) * power[k] + t * power[k + 1];
    };
    for (int p = 0; p < kPoints; ++p)
    {
        const double f = pointHz(p);
        // the 1/6-octave band, never narrower than one bin each side; averaged over 8 points per bin
        const double lo = std::min(f / sixth, f - binHz), hi = std::max(f * sixth, f + binHz);
        const int steps = std::max(2, static_cast<int>(std::ceil((hi - lo) / binHz * 8.0)));
        double sum = 0.0;
        for (int s = 0; s <= steps; ++s)
            sum += powerAt(lo + (hi - lo) * s / steps);
        const double mean = sum / (steps + 1);
        // the band's mean under-reads a lone sine by its lobe's share of the band; the peak keeps it honest
        const double peak = powerAt(f);
        const double v = std::max(mean, peak);
        levels[static_cast<size_t>(p)] = v > 1e-12 ? std::max(kFloorDb, static_cast<float>(10.0 * std::log10(v))) : kFloorDb;
    }
    fresh = true;
}
} // namespace subgen::ui
