// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <vector>

namespace subgen::dsp
{
// Integer delay of 0..maxDelay samples (0: the input itself). prepare() allocates; process() and setDelay() do not.
class DelayLine
{
public:
    void prepare(int maxDelaySamples)
    {
        buffer.assign(static_cast<size_t>(std::max(0, maxDelaySamples)) + 1, 0.0);
        pos = 0;
        delay = std::min(delay, maxDelay());
    }

    void setDelay(int samples) { delay = std::clamp(samples, 0, maxDelay()); }
    int getDelay() const { return delay; }
    int maxDelay() const { return static_cast<int>(buffer.size()) - 1; }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0);
        pos = 0;
    }

    double process(double x)
    {
        buffer[pos] = x;
        const size_t d = static_cast<size_t>(delay);
        const double y = buffer[pos >= d ? pos - d : pos + buffer.size() - d];
        pos = pos + 1 == buffer.size() ? 0 : pos + 1;
        return y;
    }

private:
    std::vector<double> buffer = std::vector<double>(1, 0.0);
    size_t pos = 0;
    int delay = 0;
};
} // namespace subgen::dsp
