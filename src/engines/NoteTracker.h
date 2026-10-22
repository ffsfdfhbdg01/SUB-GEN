// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

#include "dsp/DelayLine.h"
#include "dsp/Envelope.h"

namespace subgen::engines
{
// One sample's note boundaries. Times are in samples since NoteTracker::reset(); an end happens at the sample of the
// call that reports it.
struct NoteEvent
{
    bool start = false;
    bool end = false;
    double detect = 0.0; // when a causal detector knows the note has started (fractional)
    double begin = 0.0;  // where its rise began (<= detect; fractional when it began inside the detection sample)
};

// Note boundaries (SPEC §5.4, R2; prototype activity.note_starts / note_ends), causal, sample by sample:
// a start is a rising edge of the activity or an attack: the fast envelope (0.2/15 ms) of x_b rises 12 dB above the
// slow reference (RMS over 50 ms times sqrt 2, as it was 5 ms earlier) while it is at least sqrt 2 det_thresh; its
// begin is the sample after the last one where the fast envelope was within 1 dB of that reference (digital silence
// counts as quiet). A 50 ms lockout merges both kinds of start; an end is a falling edge of the activity. No allocation
// after prepare().
class NoteTracker
{
public:
    void prepare(double fs);
    void setThreshold(double db);
    void reset();
    NoteEvent process(double xb, bool active);

private:
    void accept(NoteEvent& ev, double t, double b);

    dsp::PeakFollower fast;
    dsp::RmsFollower slow;
    dsp::DelayLine slowDelay;
    double onsetRatio = 0.0;
    double quietRatio = 0.0;
    double minLevel = 0.0;
    double lockout = 0.0;
    std::int64_t n = 0;
    std::int64_t lastQuiet = -1;
    double lastStart = 0.0;
    bool started = false;
    double previousD = 0.0;
    bool previousCond = false;
    bool previousActive = false;
};
} // namespace subgen::engines
