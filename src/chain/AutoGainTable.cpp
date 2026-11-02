// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/AutoGainTable.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <numbers>

#include "dsp/Fft.h"

namespace subgen::chain
{
namespace
{
constexpr int kModes = 5;
constexpr int kDrives = 101;  // 0, 1, ..., 100 % (5 % is a node: alpha's kink)
constexpr int kBalances = 11; // -100, -80, ..., +100
constexpr int kOrder = 11;    // 2048 points
std::atomic<int> computed{0};
} // namespace

const AutoGainTable& AutoGainTable::instance()
{
    static const AutoGainTable table; // thread-safe one-time initialisation
    return table;
}

int AutoGainTable::computations() { return computed.load(); }

AutoGainTable::AutoGainTable()
{
    const int n = 1 << kOrder;
    power.assign(static_cast<size_t>(kModes * kDrives * kBalances * kHarmonics), 0.0);
    dsp::RealFft fft(kOrder);
    std::vector<double> y(static_cast<size_t>(n));
    std::vector<std::complex<double>> spectrum(static_cast<size_t>(n / 2 + 1));
    for (int m = 0; m < kModes; ++m)
        for (int d = 0; d < kDrives; ++d)
            for (int b = 0; b < kBalances; ++b)
            {
                const auto mode = static_cast<DriveMode>(m);
                const double drivePct = d;
                const double g = std::pow(10.0, (30.0 * drivePct / 100.0) / 20.0);
                const double alpha = std::min(1.0, drivePct / 5.0);
                const double bias = effectiveBias(mode, -100.0 + 20.0 * b);
                for (int i = 0; i < n; ++i)
                {
                    const double r = std::sin(2.0 * std::numbers::pi * i / n);
                    y[static_cast<size_t>(i)] = (1.0 - alpha) * r + alpha * shape(mode, g * r, bias);
                }
                fft.forward(y.data(), spectrum.data());
                double* out = power.data() + ((m * kDrives + d) * kBalances + b) * kHarmonics;
                for (int k = 1; k <= kHarmonics; ++k)
                {
                    const double c = 2.0 * std::abs(spectrum[static_cast<size_t>(k)]) / n;
                    out[k - 1] = c * c;
                }
            }
    computed.fetch_add(1);
}

const double* AutoGainTable::cell(int mode, int drive, int balance) const
{
    return power.data() + ((mode * kDrives + drive) * kBalances + balance) * kHarmonics;
}

double AutoGainTable::compensationDb(DriveMode mode, double drivePct, double balance, double fSub1Hz, double fs) const
{
    const double dPos = std::clamp(drivePct, 0.0, 100.0);
    const double bPos = (std::clamp(balance, -100.0, 100.0) + 100.0) / 20.0;
    const int d0 = std::min(static_cast<int>(dPos), kDrives - 2);
    const int b0 = std::min(static_cast<int>(bPos), kBalances - 2);
    const double t = dPos - d0;
    const double s = bPos - b0;
    const int m = static_cast<int>(mode);
    const double* p00 = cell(m, d0, b0);
    const double* p10 = cell(m, d0 + 1, b0);
    const double* p01 = cell(m, d0, b0 + 1);
    const double* p11 = cell(m, d0 + 1, b0 + 1);
    double meanSquare = 0.0;
    for (int k = 1; k <= kHarmonics; ++k)
    {
        const double f = k * fSub1Hz;
        if (f >= 0.5 * fs)
            break;
        const size_t i = static_cast<size_t>(k - 1);
        meanSquare += 0.5 * ((1.0 - t) * (1.0 - s) * p00[i] + t * (1.0 - s) * p10[i] + (1.0 - t) * s * p01[i] + t * s * p11[i]);
    }
    return meanSquare > 0.0 ? -10.0 * std::log10(2.0 * meanSquare) : 0.0;
}
} // namespace subgen::chain
