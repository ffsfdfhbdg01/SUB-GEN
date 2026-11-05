// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

// Parameter smoothing classes of PARAMETERS §1: L20 (linear), G20 (multiplicative, for gains, reaching 0 exactly)
// and F20 (multiplicative, for frequencies). Each ramp lasts exactly timeMs and ends exactly on the target. Common
// API: reset(fs, timeMs, value), setTarget(value), next() (advance one sample, return the value), current(),
// isSmoothing(). No allocation.
namespace subgen::dsp
{
class LinearSmoother
{
public:
    void reset(double fs, double timeMs, double value)
    {
        steps = std::max(1, static_cast<int>(std::lround(timeMs * 1e-3 * fs)));
        cur = target = value;
        remaining = 0;
    }

    void setTarget(double value)
    {
        if (value == target)
            return;
        target = value;
        remaining = steps;
        step = (target - cur) / steps;
    }

    double next()
    {
        if (remaining > 0)
            cur = --remaining == 0 ? target : cur + step;
        return cur;
    }

    double current() const { return cur; }
    bool isSmoothing() const { return remaining > 0; }

private:
    int steps = 1;
    int remaining = 0;
    double cur = 0.0;
    double target = 0.0;
    double step = 0.0;
};

namespace detail
{
// Ramp with a constant ratio per sample between values >= floor; the last step lands exactly on the target, which
// may be below the floor (a gain of exactly 0: the ramp runs to the floor, then the final step is the target).
template <double Floor>
class MultiplicativeSmoother
{
public:
    void reset(double fs, double timeMs, double value)
    {
        steps = std::max(1, static_cast<int>(std::lround(timeMs * 1e-3 * fs)));
        cur = target = value;
        remaining = 0;
    }

    void setTarget(double value)
    {
        if (value == target)
            return;
        target = value;
        remaining = steps;
        ratio = std::pow(std::max(target, Floor) / std::max(cur, Floor), 1.0 / steps);
    }

    double next()
    {
        if (remaining > 0)
            cur = --remaining == 0 ? target : std::max(cur, Floor) * ratio;
        return cur;
    }

    double current() const { return cur; }
    bool isSmoothing() const { return remaining > 0; }

private:
    int steps = 1;
    int remaining = 0;
    double cur = 0.0;
    double target = 0.0;
    double ratio = 1.0;
};
} // namespace detail

// G20: gains (>= 0); a ramp to or from 0 runs multiplicatively to -120 dB and jumps the last 1e-6 (inaudible).
using GainSmoother = detail::MultiplicativeSmoother<1e-6>;

// F20: frequencies and other positive values, a straight line in log frequency.
using LogSmoother = detail::MultiplicativeSmoother<1e-30>;
} // namespace subgen::dsp
