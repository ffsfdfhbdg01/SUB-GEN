// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace subgen
{
// The host's sample rate as the DSP uses it: 48 kHz when it is not a positive number, otherwise clamped to 8 ... 768
// kHz, so that delay lines and windows stay bounded and frequency limits keep lo <= hi.
inline double sanitizeSampleRate(double fs)
{
    return std::isfinite(fs) && fs > 0.0 ? std::clamp(fs, 8000.0, 768000.0) : 48000.0;
}
} // namespace subgen
