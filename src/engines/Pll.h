// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::engines
{
// Second-order PLL with a PI loop filter (SPEC §6.3; prototype track.pll) on x ~ A cos(phi). The phase detector is
// -x sin(phi) / (A / 2), so the loop gain does not depend on the level; the bandwidth is the closed-loop -3 dB
// point. The frequency stays within 10...500 Hz. No allocation.
class Pll
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        setBandwidth(5.0);
        seed(60.0, 0.0);
    }

    void setBandwidth(double hz, double zeta = 1.0 / std::numbers::sqrt2)
    {
        const double z = 1.0 + 2.0 * zeta * zeta;
        const double wn = 2.0 * std::numbers::pi * hz / std::sqrt(z + std::sqrt(z * z + 1.0));
        kp = 2.0 * zeta * wn;
        ki = wn * wn;
    }

    void seed(double fHz, double phaseRad)
    {
        w = 2.0 * std::numbers::pi * std::clamp(fHz, kMinHz, kMaxHz);
        phi = wrap(phaseRad);
    }

    // One sample: returns the phase estimate for it (wrapped to [-pi, pi)), then updates the loop.
    double process(double x, double amplitude)
    {
        const double estimate = phi;
        error = amplitude > 1e-9 ? std::clamp(-x * std::sin(phi) / (0.5 * amplitude), -2.0, 2.0) : 0.0;
        w = std::clamp(w + ki * error / fs, 2.0 * std::numbers::pi * kMinHz, 2.0 * std::numbers::pi * kMaxHz);
        phi = wrap(phi + (w + kp * error) / fs);
        return estimate;
    }

    // The last phase-detector output: sin(phase error) plus a ripple at twice the input frequency.
    double lastError() const { return error; }
    double frequencyHz() const { return w / (2.0 * std::numbers::pi); }
    double phase() const { return phi; }

private:
    static constexpr double kMinHz = 10.0;
    static constexpr double kMaxHz = 500.0;

    static double wrap(double p)
    {
        return p - 2.0 * std::numbers::pi * std::floor((p + std::numbers::pi) / (2.0 * std::numbers::pi));
    }

    double fs = 48000.0;
    double kp = 0.0;
    double ki = 0.0;
    double w = 0.0;
    double phi = 0.0;
    double error = 0.0;
};
} // namespace subgen::engines
