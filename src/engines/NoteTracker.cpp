// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/NoteTracker.h"

#include <algorithm>
#include <cmath>

#include "core/Quality.h"

namespace subgen::engines
{
void NoteTracker::prepare(double fs)
{
    fast.prepare(fs, 0.2, 15.0);
    slow.prepare(fs, 50.0);
    slowDelay.prepare(msToSamples(5.0, fs));
    slowDelay.setDelay(msToSamples(5.0, fs));
    onsetRatio = std::pow(10.0, 12.0 / 20.0);
    quietRatio = std::pow(10.0, 1.0 / 20.0);
    lockout = 50.0 * 1e-3 * fs;
    setThreshold(-55.0);
    reset();
}

void NoteTracker::setThreshold(double db)
{
    minLevel = std::sqrt(2.0) * std::pow(10.0, db / 20.0);
}

void NoteTracker::reset()
{
    fast.reset();
    slow.reset();
    slowDelay.reset();
    n = 0;
    lastQuiet = -1;
    lastStart = 0.0;
    started = false;
    previousD = 0.0;
    previousCond = false;
    previousActive = false;
}

void NoteTracker::accept(NoteEvent& ev, double t, double b)
{
    if (started && t - lastStart < lockout)
        return;
    ev.start = true;
    ev.detect = t;
    ev.begin = std::min(b, t);
    lastStart = t;
    started = true;
}

NoteEvent NoteTracker::process(double xb, bool active)
{
    NoteEvent ev;
    const double eFast = fast.process(xb);
    const double eSlow = slowDelay.process(std::sqrt(2.0) * slow.process(xb)); // the slow level 5 ms ago
    const double d = eFast - onsetRatio * eSlow;
    const bool cond = d >= 0.0 && eFast >= minLevel;
    if (eFast <= quietRatio * eSlow)
        lastQuiet = n;
    const auto i = static_cast<double>(n);

    // Both kinds of start at this sample, in time order (an attack lies in (n - 1, n], the activity edge at n).
    if (cond && !previousCond)
    {
        const double t = (n > 0 && previousD < 0.0) ? (i - 1.0) + previousD / (previousD - d) : i;
        accept(ev, t, static_cast<double>(lastQuiet + 1));
    }
    if (active && !previousActive)
        accept(ev, i, i);
    if (!active && previousActive)
        ev.end = true;

    previousD = d;
    previousCond = cond;
    previousActive = active;
    ++n;
    return ev;
}
} // namespace subgen::engines
