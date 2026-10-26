// SPDX-License-Identifier: AGPL-3.0-or-later
#include "analysis/AutoAlign.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

#include "dsp/Fft.h"

namespace subgen::analysis
{
namespace
{
constexpr double kTieDb = 1e-3;
constexpr double kClearDb = 0.5;
constexpr double kFloor = 1e-300;

double wrapDeg(double a) // into [-180, 180), as Python's (a + 180) % 360 - 180
{
    const double m = std::fmod(a + 180.0, 360.0);
    return (m < 0.0 ? m + 360.0 : m) - 180.0;
}

int ceilLog2(size_t n)
{
    int e = 0;
    while ((size_t{1} << e) < n)
        ++e;
    return e;
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const size_t h = v.size() / 2;
    return v.size() % 2 == 1 ? v[h] : 0.5 * (v[h - 1] + v[h]);
}
} // namespace

AlignResult autoAlign(std::span<const double> dryBand, std::span<const double> subAfterA, double fs, double rangeLoHz,
                      double rangeHiHz, AlignObjective objective, double stepDeg)
{
    const size_t n = std::min(dryBand.size(), subAfterA.size());
    if (n < 4 || !std::isfinite(fs) || fs <= 0.0)
        return {};
    const double step = std::isfinite(stepDeg) && stepDeg > 0.0 ? std::clamp(stepDeg, 0.1, 180.0) : 15.0;
    const auto rotations = static_cast<size_t>(std::max(1L, std::lround(360.0 / step)));
    const int order = std::max(ceilLog2(n), 1);
    const size_t m = size_t{1} << order;

    // the analytic signal of the sub: its spectrum with the negative frequencies removed
    std::vector<std::complex<double>> a(m);
    for (size_t i = 0; i < n; ++i)
        a[i] = std::isfinite(subAfterA[i]) ? subAfterA[i] : 0.0;
    dsp::ComplexFft fft(order);
    fft.forward(a.data());
    for (size_t k = 1; k < m / 2; ++k)
        a[k] *= 2.0;
    for (size_t k = m / 2 + 1; k < m; ++k)
        a[k] = 0.0;
    fft.inverse(a.data());
    auto dryAt = [&](size_t i) { return std::isfinite(dryBand[i]) ? dryBand[i] : 0.0; };

    std::vector<double> psi(rotations), score(rotations);
    for (size_t k = 0; k < rotations; ++k)
        psi[k] = static_cast<double>(k) * (360.0 / static_cast<double>(rotations));
    if (objective == AlignObjective::Energy)
    {
        // z's energy in the overlap band, from the Hann-windowed spectra of d, Re a and Im a
        const double lo = std::max(0.5 * rangeLoHz, 20.0);
        const double hi = rangeHiHz;
        dsp::RealFft rfft(order);
        std::vector<double> buffer(m);
        auto spectrum = [&](auto value) {
            std::fill(buffer.begin(), buffer.end(), 0.0);
            for (size_t i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1));
                buffer[i] = w * value(i);
            }
            std::vector<std::complex<double>> out(m / 2 + 1);
            rfft.forward(buffer.data(), out.data());
            std::vector<std::complex<double>> kept;
            for (size_t k = 0; k <= m / 2; ++k)
            {
                const double f = static_cast<double>(k) * fs / static_cast<double>(m);
                if (f >= lo && f <= hi)
                    kept.push_back(out[k]);
            }
            return kept;
        };
        const auto d = spectrum(dryAt);
        const auto re = spectrum([&](size_t i) { return a[i].real(); });
        const auto im = spectrum([&](size_t i) { return a[i].imag(); });
        for (size_t k = 0; k < rotations; ++k)
        {
            const double rad = psi[k] * std::numbers::pi / 180.0;
            const double c = std::cos(rad), s = std::sin(rad);
            double energy = 0.0;
            for (size_t b = 0; b < d.size(); ++b)
                energy += std::norm(d[b] + c * re[b] - s * im[b]);
            score[k] = 10.0 * std::log10(std::max(energy, kFloor)); // larger is better
        }
    }
    else
    {
        for (size_t k = 0; k < rotations; ++k)
        {
            const double rad = psi[k] * std::numbers::pi / 180.0;
            const double c = std::cos(rad), s = std::sin(rad);
            double peak = 0.0;
            for (size_t i = 0; i < n; ++i)
                peak = std::max(peak, std::abs(dryAt(i) + c * a[i].real() - s * a[i].imag()));
            score[k] = -20.0 * std::log10(std::max(peak, kFloor)); // a lower peak is better
        }
    }
    // the best, ties within 1e-3 dB going to the rotation nearest the identity (+ before -)
    const double best = *std::max_element(score.begin(), score.end());
    size_t pick = 0;
    double nearest = std::numeric_limits<double>::infinity();
    for (size_t k = 0; k < rotations; ++k)
    {
        if (score[k] < best - kTieDb)
            continue;
        const double w = wrapDeg(psi[k]);
        const double distance = std::abs(w) + (w < 0.0 ? 1e-9 : 0.0);
        if (distance < nearest)
        {
            nearest = distance;
            pick = k;
        }
    }
    AlignResult r;
    const double canonical = wrapDeg(psi[pick]);
    if (canonical > -90.0 && canonical <= 90.0)
        r.rotationDeg = canonical;
    else
    {
        r.polarity = -1;
        r.rotationDeg = wrapDeg(canonical - 180.0);
    }
    r.gainDb = score[pick] - score[0];
    r.confidenceDb = score[pick] - median(score);
    r.clearOptimum = r.confidenceDb >= kClearDb;
    return r;
}
} // namespace subgen::analysis
