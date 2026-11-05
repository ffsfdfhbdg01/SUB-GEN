// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/PitchMpm.h"

#include <algorithm>
#include <cmath>

#include "dsp/FilterDesign.h"

namespace subgen::engines
{
namespace
{
constexpr double kTargetRate = 2000.0;
constexpr double kHopSeconds = 2e-3;
constexpr double kWindowPeriods = 2.5;
constexpr double kTrimRel = 0.01;     // leading samples below this fraction of the frame peak count as silence
constexpr double kKeyMax = 0.9;       // MPM key-maximum threshold
constexpr double kClarityFloor = 0.6; // clarity at or below this is confidence 0 (band-limited noise: 0.45-0.56)
} // namespace

void PitchMpm::prepare(double fs, double fMaxHz)
{
    decim = std::max(1, static_cast<int>(std::floor(fs / kTargetRate + 1e-9)));
    fsD = fs / decim;
    hop = std::max(1, static_cast<int>(std::nearbyint(kHopSeconds * fsD)));
    const double fMax = std::isfinite(fMaxHz) && fMaxHz > 0.0 ? fMaxHz : 300.0;
    tauMin = std::max(2, static_cast<int>(std::floor(fsD / fMax)));
    maxWindow = static_cast<int>(std::ceil(kWindowPeriods * fsD / 20.0));
    if (decim > 1)
        antiAlias.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::LowPass, 8, 800.0, fs));
    const auto size = static_cast<size_t>(maxWindow);
    ring.assign(size, 0.0);
    frame.assign(size, 0.0);
    nsdf.assign(size, 0.0);
    energy.assign(size + 1, 0.0);
    peakLag.assign(size, 0);
    peakHeight.assign(size, 0.0);
    setMinFrequency(20.0);
    reset();
}

void PitchMpm::setMinFrequency(double rangeLoHz)
{
    const double fMin = std::isfinite(rangeLoHz) ? std::clamp(rangeLoHz, 15.0, 250.0) : 20.0;
    window = std::min(maxWindow, static_cast<int>(std::ceil(kWindowPeriods * fsD / std::max(fMin, 20.0))));
    tauMax = static_cast<int>(std::ceil(fsD / fMin));
}

void PitchMpm::reset()
{
    antiAlias.reset();
    std::fill(ring.begin(), ring.end(), 0.0);
    inputCount = 0;
    decimatedCount = 0;
    lastY = 0.0;
}

int PitchMpm::process(std::span<const double> xb, std::span<PitchFrame> out)
{
    int written = 0;
    for (const double x : xb)
    {
        const double y = decim > 1 ? antiAlias.process(x) : x;
        if (inputCount++ % decim != 0)
            continue;
        const std::int64_t j = decimatedCount++;
        ring[static_cast<size_t>(j % maxWindow)] = y;
        lastY = y;
        if (j % hop == 0 && written < static_cast<int>(out.size()))
            out[static_cast<size_t>(written++)] = analyse(j);
    }
    return written;
}

PitchFrame PitchMpm::analyse(std::int64_t j)
{
    PitchFrame fr;
    fr.timeSamples = static_cast<double>(j * decim);
    double peak = 0.0;
    for (int i = 0; i < window; ++i)
    {
        const std::int64_t q = j - (window - 1) + i;
        const double v = q >= 0 ? ring[static_cast<size_t>(q % maxWindow)] : 0.0;
        frame[static_cast<size_t>(i)] = v;
        peak = std::max(peak, std::abs(v));
    }
    int start = window; // a silent frame has nothing left
    if (peak != 0.0)
        for (start = 0; !(std::abs(frame[static_cast<size_t>(start)]) > kTrimRel * peak); ++start)
        {
        }
    const int n = window - start;
    const int tauHi = std::min(tauMax, n / 2);
    double lag = 0.0;
    double height = 0.0;
    if (tauHi < tauMin + 2 || !pick(frame.data() + start, n, tauHi, lag, height))
        return fr;
    fr.f0Hz = fsD / lag;
    const double clarity = std::min(std::max(height, 0.0), 1.0);
    fr.confidence = std::clamp((clarity - kClarityFloor) / (1.0 - kClarityFloor), 0.0, 1.0);
    return fr;
}

// The first key maximum of the NSDF within [tauMin, tauHi] reaching 0.9 of the highest one (track._mpm_pick): the
// lobe around lag 0 is skipped, each later positive lobe starting by tauHi gives its maximum if that is a local one
// inside [tauMin, tauHi]. The NSDF is computed up to the end of the lobe that holds tauHi; later lags cannot matter.
bool PitchMpm::pick(const double* g, int n, int tauHi, double& lag, double& height)
{
    energy[0] = 0.0;
    for (int i = 0; i < n; ++i)
        energy[static_cast<size_t>(i) + 1] = energy[static_cast<size_t>(i)] + g[i] * g[i];
    const double total = energy[static_cast<size_t>(n)];
    double* s = nsdf.data();
    int computed = 0;
    for (int tau = 0; tau < n; ++tau)
    {
        double r = 0.0;
        for (int q = 0; q + tau < n; ++q)
            r += g[q] * g[q + tau];
        // n'(tau) = 2 r'(tau) / m'(tau), m' = sum of g_j^2 + g_{j+tau}^2 over the overlap
        const double m = energy[static_cast<size_t>(n - tau)] + (total - energy[static_cast<size_t>(tau)]);
        s[tau] = m > 0.0 ? 2.0 * r / m : 0.0;
        computed = tau + 1;
        if (tau > tauHi + 1 && s[tau] < 0.0)
            break;
    }
    int firstDown = -1; // the end of the lobe around lag 0
    for (int q = 1; q < computed && firstDown < 0; ++q)
        if (s[q - 1] >= 0.0 && s[q] < 0.0)
            firstDown = q;
    if (firstDown < 0)
        return false;
    int peaks = 0;
    for (int u = firstDown + 1; u < computed && u <= tauHi; ++u)
    {
        if (!(s[u - 1] < 0.0 && s[u] >= 0.0))
            continue;
        int end = u + 1;
        while (end < computed && s[end] >= 0.0)
            ++end;
        int p = u;
        for (int q = u + 1; q < end; ++q)
            if (s[q] > s[p])
                p = q;
        if (p >= tauMin && p <= tauHi && p >= 1 && p <= n - 2 && s[p] >= s[p - 1] && s[p] >= s[p + 1])
        {
            peakLag[static_cast<size_t>(peaks)] = p;
            peakHeight[static_cast<size_t>(peaks)] = s[p];
            ++peaks;
        }
        u = end - 1;
    }
    if (peaks == 0)
        return false;
    double top = peakHeight[0];
    for (int i = 1; i < peaks; ++i)
        top = std::max(top, peakHeight[static_cast<size_t>(i)]);
    int chosen = 0;
    while (!(peakHeight[static_cast<size_t>(chosen)] >= kKeyMax * top))
        ++chosen;
    const int p = peakLag[static_cast<size_t>(chosen)];
    // the parabola through (-1, a), (0, b), (1, c)
    const double a = s[p - 1];
    const double b = s[p];
    const double c = s[p + 1];
    const double den = a - 2.0 * b + c;
    const double delta = den != 0.0 ? 0.5 * (a - c) / den : 0.0;
    lag = p + delta;
    height = b - 0.25 * (a - c) * delta;
    return true;
}
} // namespace subgen::engines
