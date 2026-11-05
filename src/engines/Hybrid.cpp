// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/Hybrid.h"

#include <algorithm>

namespace subgen::engines
{
void Hybrid::prepare(double fs, int maxBlock, int lookahead)
{
    divide.prepare(fs, maxBlock, lookahead);
    harmonic.prepare(fs, maxBlock, lookahead);
    scratch.assign(static_cast<size_t>(std::max(1, maxBlock)), 0.0);
}

void Hybrid::reset()
{
    divide.reset();
    harmonic.reset();
}

void Hybrid::setControl(const EngineControl& requested)
{
    control = clampControl(requested);
    divide.setControl(requested);
    harmonic.setControl(requested);
}

void Hybrid::process(std::span<const double> source, std::span<const double> key, std::span<double> out)
{
    if (scratch.empty()) // not prepared: silence
    {
        std::fill(out.begin(), out.end(), 0.0);
        return;
    }
    for (size_t pos = 0; pos < out.size(); pos += scratch.size())
    {
        const size_t len = std::min(scratch.size(), out.size() - pos);
        const auto src = pos < source.size() ? source.subspan(pos, std::min(len, source.size() - pos))
                                             : std::span<const double>{};
        const auto k = pos < key.size() ? key.subspan(pos, std::min(len, key.size() - pos)) : std::span<const double>{};
        const auto o = out.subspan(pos, len);
        const auto h = std::span<double>(scratch.data(), len);
        divide.process(src, k, o);
        harmonic.process(src, k, h);
        for (size_t i = 0; i < len; ++i)
            o[i] = control.hybDivide * o[i] + control.hybHarmonic * h[i];
    }
}
} // namespace subgen::engines
