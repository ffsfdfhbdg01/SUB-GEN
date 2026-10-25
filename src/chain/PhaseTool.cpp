// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/PhaseTool.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::chain
{
namespace
{
// hilbert.PAIR of the prototype (rad/s): A 4.047, 30.298, 119.010, 514.848 Hz; B 13.985, 60.499, 237.641, 1778.969 Hz
constexpr std::array<double, 4> kPolesA{25.429860045190566, 190.3669971504519, 747.761428356882, 3234.885596037998};
constexpr std::array<double, 4> kPolesB{87.86851909071692, 380.12739889035265, 1493.1401503758816, 11177.592257549077};
constexpr double kRotationMs = 20.0;
constexpr double kPolarityMs = 5.0;

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double coefficient(double pole, double fs)
{
    const double g = pole / (2.0 * fs);
    return (g - 1.0) / (g + 1.0);
}

// one first-order all-pass in transposed direct form II, as scipy.signal.lfilter([c, 1], [1, c])
double allPass(double x, double c, double& z)
{
    const double y = c * x + z;
    z = x - c * y;
    return y;
}
} // namespace

void HilbertPair::prepare(double sampleRate)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    for (size_t i = 0; i < coeffA.size(); ++i)
    {
        coeffA[i] = coefficient(kPolesA[i], fs);
        coeffB[i] = coefficient(kPolesB[i], fs);
    }
    reset();
}

void HilbertPair::reset()
{
    stateA.fill(0.0);
    stateB.fill(0.0);
}

void HilbertPair::process(double x, double& a, double& b)
{
    a = x;
    b = x;
    for (size_t i = 0; i < coeffA.size(); ++i)
    {
        a = allPass(a, coeffA[i], stateA[i]);
        b = allPass(b, coeffB[i], stateB[i]);
    }
}

double HilbertPair::groupDelayMsA(double fHz) const
{
    // a first-order all-pass delays by (1 - a^2) / (1 + 2 a cos w + a^2) samples
    const double cw = std::cos(2.0 * std::numbers::pi * fHz / fs);
    double samples = 0.0;
    for (const double c : coeffA)
        samples += (1.0 - c * c) / (1.0 + 2.0 * c * cw + c * c);
    return 1000.0 * samples / fs;
}

void PhaseTool::prepare(double sampleRate)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    pair.prepare(fs);
    rotation.reset(fs, kRotationMs, 0.0);
    polarity.reset(fs, kPolarityMs, 0.0);
    madeDeg = 0.0;
    cosT = 1.0;
    sinT = 0.0;
    fresh = true;
}

void PhaseTool::reset()
{
    pair.reset();
    fresh = true;
}

void PhaseTool::setParams(const PhaseParams& p)
{
    const double deg = std::isfinite(p.rotationDeg) ? std::clamp(p.rotationDeg, -180.0, 180.0) : 0.0;
    const double inverted = p.invert ? 1.0 : 0.0;
    if (fresh)
    {
        fresh = false;
        rotation.reset(fs, kRotationMs, deg);
        polarity.reset(fs, kPolarityMs, inverted);
        return;
    }
    rotation.setTarget(deg);
    polarity.setTarget(inverted);
}

void PhaseTool::process(std::span<double> block)
{
    for (auto& x : block)
    {
        double a = 0.0, b = 0.0;
        pair.process(x, a, b);
        const double deg = rotation.next();
        if (deg != madeDeg)
        {
            const double rad = deg * std::numbers::pi / 180.0;
            cosT = std::cos(rad);
            sinT = std::sin(rad);
            madeDeg = deg;
        }
        const double u = polarity.next();
        const double gain = u == 0.0 ? 1.0 : 1.0 - 2.0 * sCurve(u); // +1 ... -1 exactly at the ends
        x = gain * (cosT * a + sinT * b);
    }
}

void PhaseTool::chainA(std::span<const double> in, std::span<double> out) const
{
    const auto& c = pair.coefficientsA();
    std::array<double, 4> z{};
    for (size_t n = 0; n < in.size(); ++n)
    {
        double v = in[n];
        for (size_t i = 0; i < c.size(); ++i)
            v = allPass(v, c[i], z[i]);
        out[n] = v;
    }
}
} // namespace subgen::chain
