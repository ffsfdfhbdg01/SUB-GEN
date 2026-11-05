// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::chain
{
enum class DriveMode
{
    Soft,
    Tube,
    Tape,
    Fold,
    Hard
};

namespace drive_detail
{
// ln cosh x, stable for large |x|: |x| + log1p(exp(-2 |x|)) - ln 2
inline double logCosh(double x)
{
    const double ax = std::abs(x);
    return ax + std::log1p(std::exp(-2.0 * ax)) - std::numbers::ln2;
}

inline double base(DriveMode mode, double x)
{
    switch (mode)
    {
    case DriveMode::Tape:
        return (2.0 / std::numbers::pi) * std::atan(std::numbers::pi * x / 2.0);
    case DriveMode::Fold:
        return std::sin(std::numbers::pi * x / 2.0);
    case DriveMode::Hard:
        return std::clamp(x, -1.0, 1.0);
    case DriveMode::Soft:
    case DriveMode::Tube:
        break;
    }
    return std::tanh(x);
}

inline double baseIntegral(DriveMode mode, double x)
{
    switch (mode)
    {
    case DriveMode::Tape:
    {
        const double h = std::numbers::pi * x / 2.0;
        return (2.0 / std::numbers::pi) * x * std::atan(h) -
               (2.0 / (std::numbers::pi * std::numbers::pi)) * std::log1p(h * h);
    }
    case DriveMode::Fold:
        return -(2.0 / std::numbers::pi) * std::cos(std::numbers::pi * x / 2.0);
    case DriveMode::Hard:
        return std::abs(x) <= 1.0 ? 0.5 * x * x : std::abs(x) - 0.5;
    case DriveMode::Soft:
    case DriveMode::Tube:
        break;
    }
    return logCosh(x);
}
} // namespace drive_detail

// The bias of SPEC §7.3 (drive_balance in -100...+100): b_mode (1 - max(0, balance) / 100) + 0.5 max(0, -balance) /
// 100, b_mode = 0.35 for Tube (Soft with a bias) and 0 otherwise.
inline double effectiveBias(DriveMode mode, double balance)
{
    const double bMode = mode == DriveMode::Tube ? 0.35 : 0.0;
    return bMode * (1.0 - std::max(0.0, balance) / 100.0) + 0.5 * std::max(0.0, -balance) / 100.0;
}

// The shapers of SPEC §7.3 with a bias b: f_b(x) = f(x + b) - f(b) (Soft tanh, Tape (2/pi) atan(pi x / 2), Fold
// sin(pi x / 2), Hard clamp to +-1) ...
inline double shape(DriveMode mode, double x, double bias)
{
    return drive_detail::base(mode, x + bias) - drive_detail::base(mode, bias);
}

// ... and their antiderivatives for ADAA: F1_b(x) = F1(x + b) - x f(b).
inline double antiderivative(DriveMode mode, double x, double bias)
{
    return drive_detail::baseIntegral(mode, x + bias) - x * drive_detail::base(mode, bias);
}
} // namespace subgen::chain
