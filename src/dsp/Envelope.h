// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Envelope followers and causal sliding statistics (prototype: proto/subgen_proto/envelope.py). prepare() allocates;
// process() and the setters do not.
namespace subgen::dsp
{
// Rectified one-pole follower with separate attack and release time constants (envelope.peak_follower).
class PeakFollower
{
public:
    void prepare(double fs, double attackMs, double releaseMs)
    {
        attack = 1.0 - std::exp(-1.0 / (attackMs * 1e-3 * fs));
        release = 1.0 - std::exp(-1.0 / (releaseMs * 1e-3 * fs));
        y = 0.0;
    }
    void reset() { y = 0.0; }
    double current() const { return y; }

    double process(double x)
    {
        const double v = std::abs(x);
        y += (v > y ? attack : release) * (v - y);
        return y;
    }

private:
    double attack = 1.0;
    double release = 1.0;
    double y = 0.0;
};

// sqrt of a one-pole average of x^2 with time constant tauMs (envelope.rms_follower).
class RmsFollower
{
public:
    void prepare(double fs, double tauMs)
    {
        a = 1.0 - std::exp(-1.0 / (tauMs * 1e-3 * fs));
        meanSquare = 0.0;
    }
    void reset() { meanSquare = 0.0; }
    double current() const { return std::sqrt(meanSquare); }

    double process(double x)
    {
        meanSquare = a * (x * x) + (1.0 - a) * meanSquare;
        return std::sqrt(meanSquare);
    }

private:
    double a = 1.0;
    double meanSquare = 0.0;
};

// Causal running maximum of the last w samples, w <= maxWindow (envelope.sliding_peak); samples before the start
// count as the value given to reset(). A monotonic deque in a ring buffer keeps every sample a window of up to
// maxWindow could still need, so a window change takes effect at once and exactly; the answer for the current window
// is found by a binary search over the deque (indices increase from the front): O(log maxWindow) per sample.
class SlidingMax
{
public:
    void prepare(int maxWindowSamples)
    {
        maxWindow = std::max(1, maxWindowSamples);
        ring.assign(static_cast<size_t>(maxWindow) + 1, Entry{});
        window = std::clamp(window, 1, maxWindow);
        reset();
    }

    void setWindow(int w) { window = std::clamp(w, 1, maxWindow); }
    int getWindow() const { return window; }

    void reset(double initial = 0.0)
    {
        head = 0;
        size = 1;
        ring[0] = {-1, initial}; // stands for every sample before the start
        n = 0;
    }

    double process(double x)
    {
        while (size > 0 && at(size - 1).value <= x)
            --size;
        at(size++) = {n, x};
        while (at(0).index <= n - maxWindow)
        {
            head = (head + 1) % ring.size();
            --size;
        }
        const std::int64_t oldest = n - window + 1;
        size_t lo = 0;
        size_t hi = size - 1;
        while (lo < hi)
        {
            const size_t mid = (lo + hi) / 2;
            if (at(mid).index >= oldest)
                hi = mid;
            else
                lo = mid + 1;
        }
        ++n;
        return at(lo).value;
    }

private:
    struct Entry
    {
        std::int64_t index = 0;
        double value = 0.0;
    };

    Entry& at(size_t i) { return ring[(head + i) % ring.size()]; }

    std::vector<Entry> ring = std::vector<Entry>(2);
    size_t head = 0;
    size_t size = 0;
    int maxWindow = 1;
    int window = 1;
    std::int64_t n = 0;
};

// Mean of x^2 over the last w samples (zeros before the start). A running sum, recomputed exactly every w samples and
// on every window change, so it does not drift.
class SlidingMeanSquare
{
public:
    void prepare(int maxWindowSamples)
    {
        squares.assign(static_cast<size_t>(std::max(1, maxWindowSamples)), 0.0);
        window = std::clamp(window, 1, static_cast<int>(squares.size()));
        reset();
    }

    void setWindow(int w)
    {
        window = std::clamp(w, 1, static_cast<int>(squares.size()));
        recompute();
    }

    void reset()
    {
        std::fill(squares.begin(), squares.end(), 0.0);
        pos = 0;
        sum = 0.0;
        sinceExact = 0;
    }

    double process(double x)
    {
        const size_t cap = squares.size();
        const double s = x * x;
        sum += s - squares[(pos + cap - static_cast<size_t>(window)) % cap];
        squares[pos] = s;
        pos = (pos + 1) % cap;
        if (++sinceExact >= window)
            recompute();
        return sum / window;
    }

private:
    void recompute()
    {
        const size_t cap = squares.size();
        double exact = 0.0;
        for (int i = 1; i <= window; ++i)
            exact += squares[(pos + cap - static_cast<size_t>(i)) % cap];
        sum = exact;
        sinceExact = 0;
    }

    std::vector<double> squares = std::vector<double>(1, 0.0);
    size_t pos = 0;
    int window = 1;
    double sum = 0.0;
    int sinceExact = 0;
};
} // namespace subgen::dsp
