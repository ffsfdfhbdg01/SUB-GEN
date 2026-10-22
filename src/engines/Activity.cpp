// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/Activity.h"

#include <cmath>

#include "core/Quality.h"

namespace subgen::engines
{
void Activity::prepare(double fs)
{
    rms.prepare(fs, 20.0);
    peak.prepare(fs, 1.0, 50.0);
    hold = msToSamples(20.0, fs);
    releaseCoeff = std::exp(-1.0 / (0.020 * fs));
    setThreshold(-55.0);
    reset();
}

void Activity::setThreshold(double db)
{
    onLevel = std::pow(10.0, db / 20.0);
    offLevel = std::pow(10.0, (db - 3.0) / 20.0);
}

void Activity::reset()
{
    rms.reset();
    peak.reset();
    below = 0;
    state = false;
    gain = 0.0;
}
} // namespace subgen::engines
