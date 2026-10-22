// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/Divide.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/Quality.h"

namespace subgen::engines
{
namespace
{
constexpr double kArmingFloor = 3.1622776601683795e-05; // 10^(-90/20)
constexpr double kProvisionalConf = 1.0;
constexpr double kResidualScale = 0.15;
const double kConstantLevel = std::pow(10.0, -12.0 / 20.0); // Constant: -12 dBFS peak

// numpy.median of up to five values (mean of the two middle ones for an even count)
template <size_t N>
double median(const std::array<double, N>& values, int count)
{
    std::array<double, N> v = values;
    std::sort(v.begin(), v.begin() + count);
    return count % 2 == 1 ? v[static_cast<size_t>(count / 2)]
                          : (v[static_cast<size_t>(count / 2 - 1)] + v[static_cast<size_t>(count / 2)]) / 2.0;
}

// newest last; keeps the last N values
template <size_t N>
void pushLast(std::array<double, N>& values, int& count, double value)
{
    if (count < static_cast<int>(N))
    {
        values[static_cast<size_t>(count++)] = value;
        return;
    }
    std::rotate(values.begin(), values.begin() + 1, values.end());
    values[N - 1] = value;
}

const double kSection1 = 1.0 / (1.0 / (2.0 * std::sin(std::numbers::pi / 8.0)));       // k = 1/Q of the LP4 sections
const double kSection2 = 1.0 / (1.0 / (2.0 * std::sin(3.0 * std::numbers::pi / 8.0)));
} // namespace

namespace divide_detail
{
void Core::configure(double sampleRate)
{
    fs = sampleRate;
    minGap = 0.04 * fs;
    clearState();
}

void Core::clearState()
{
    const double keepFs = fs;
    const double keepGap = minGap;
    *this = Core{};
    fs = keepFs;
    minGap = keepGap;
}

void Core::store(std::int64_t index, double f, double c, double period, bool prov)
{
    heldIndex = index;
    heldEnd = index + static_cast<std::int64_t>(period > 0.0 ? std::max(2.5 * period, minGap) : minGap);
    heldF = f;
    heldConf = c;
    heldProvisional = prov;
}

void Core::clear(std::int64_t k, SignOp kind, double confAfter, std::int64_t negBefore, std::int64_t posBefore,
                 std::array<SignOp, 4>& ops, int& count)
{
    ops[static_cast<size_t>(count++)] = kind;
    ++resets;
    resetNow = true;
    store(k, 0.0, confAfter, 0.0, confAfter == kProvisionalConf);
    baseNeg = negBefore; // the counts at k - 1
    basePos = posBefore;
    s1 = 1;
    pending = false;
    crossings = 0;
    periodCount = 0;
    residualCount = 0;
    hasSmoothedF = false;
}

void Core::start(std::int64_t k, bool ramp, double time, std::int64_t negBefore, std::int64_t posBefore,
                 std::array<SignOp, 4>& ops, int& count)
{
    clear(k, ramp ? SignOp::Ramp : SignOp::Switch, kProvisionalConf, negBefore, posBefore, ops, count);
    running = true;
    provisional = true;
    endPending = false;
    lastStartTime = time;
}

void Core::risingFired(std::int64_t idx, double t)
{
    ++crossings;
    double f = 0.0;
    double c = provisional ? kProvisionalConf : 0.0;
    bool prov = provisional;
    double period = 0.0;
    if (crossings >= 2)
    {
        period = t - lastCrossing;
        pushLast(periods, periodCount, period);
        const int used = std::min(periodCount, 3);
        const double fMed = fs / median(periods, used);
        if (!hasSmoothedF)
        {
            smoothedF = fMed;
            hasSmoothedF = true;
        }
        else
            smoothedF += (1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 * period / fs)) * (fMed - smoothedF);
        if (periodCount >= 3)
        {
            // deviation from the linear period trend 2 T[-2] - T[-3] (periods holds the last three, oldest first)
            const double r = std::abs(periods[2] - (2.0 * periods[1] - periods[0])) / periods[1];
            pushLast(residuals, residualCount, r);
        }
        if (residualCount > 0)
        {
            prov = provisional = false;
            c = 1.0 - std::min(1.0, median(residuals, std::min(residualCount, 5)) / kResidualScale);
        }
        f = smoothedF;
        lastPeriod = period;
    }
    lastCrossing = t;
    store(idx, f, c, period, prov);
}

int Core::step(double x, double theta, const SampleEvents& ev, std::array<SignOp, 4>& ops)
{
    int count = 0;
    resetNow = false;
    const std::int64_t i = next;
    const std::int64_t negBefore = csNeg; // counts through i - 1
    const std::int64_t posBefore = csPos;
    if (ev.startBefore)
        start(i, ev.ramp, ev.startTime, negBefore, posBefore, ops, count);

    bool firedRise = false;
    bool firedFall = false;
    if (i >= 1)
    {
        const bool rising = prevX < 0.0 && x >= 0.0;
        const bool falling = prevX > 0.0 && x <= 0.0;
        if (rising || falling)
        {
            if (endPending) // the note ended: reset at this crossing, then stay frozen
            {
                clear(i, SignOp::Switch, 0.0, negBefore, posBefore, ops, count);
                running = false;
                provisional = false;
                endPending = false;
            }
            else if (running && (falling ? posBefore - basePos > 0 : negBefore - baseNeg > 0))
            {
                const double t = static_cast<double>(i - 1) + prevX / (prevX - x);
                if (falling)
                {
                    firedFall = true;
                    if (pending)
                    {
                        ops[static_cast<size_t>(count++)] = SignOp::Flip2;
                        pending = false;
                    }
                }
                else
                {
                    firedRise = true;
                    ops[static_cast<size_t>(count++)] = SignOp::Flip1;
                    s1 = -s1;
                    if (s1 == 1)
                        pending = true;
                    risingFired(i, t);
                }
            }
        }
    }
    csNeg += x < -theta ? 1 : 0;
    csPos += x > theta ? 1 : 0;
    if (firedFall)
        basePos = csPos; // the counts at the fired crossing, inclusive
    if (firedRise)
        baseNeg = csNeg;

    // an end is cancelled by a start already applied (at its reset point) whose processing time is later
    if (ev.end && !(lastStartTime > static_cast<double>(i)))
        endPending = true;
    if (ev.startAfter)
        start(i, ev.ramp, ev.startTime, negBefore, posBefore, ops, count);
    prevX = x;
    ++next;
    return count;
}
} // namespace divide_detail

using divide_detail::SampleEvents;
using divide_detail::SignOp;

void Divide::prepare(double sampleRate, int, int lookahead)
{
    fs = sampleRate;
    look = std::max(0, lookahead);
    rampLength = std::max(1, msToSamples(0.5, fs));
    slewCoeff = 1.0 - std::exp(-1.0 / (0.005 * fs));
    band.prepare(fs);
    activity.prepare(fs);
    notes.prepare(fs);
    gate.prepare(fs);
    constantPeak.prepare(static_cast<int>(std::ceil(1.25 * fs / 15.0)) + 1);
    ringXb.assign(static_cast<size_t>(look) + 4, 0.0);
    ringTheta.assign(static_cast<size_t>(look) + 4, 0.0);
    coreA.configure(fs);
    setControl(control);
    reset();
}

void Divide::reset()
{
    band.reset();
    activity.reset();
    notes.reset();
    gate.reset();
    constantPeak.reset();
    std::fill(ringXb.begin(), ringXb.end(), 0.0);
    std::fill(ringTheta.begin(), ringTheta.end(), 0.0);
    queueCount = 0;
    coreA.clearState();
    coreB = coreA;
    signs = Signs{};
    smooth1 = Smoother{};
    smooth2 = Smoother{};
    fHeld = fInit;
    fConstant = 0.0;
    n = 0;
    lastFEst = 0.0;
    lastConfKnown = 0.0;
    lastProbe = DivideProbe{};
}

void Divide::setControl(const EngineControl& c)
{
    control = c;
    if (c.baseFixed)
        band.setBandpass(2.0 * c.baseFreqHz); // Sub -1 at base_freq: the source an octave higher
    else
        band.setRange(c.rangeLoHz, c.rangeHiHz, c.rangeSlope);
    activity.setThreshold(c.detThreshDb);
    notes.setThreshold(c.detThreshDb);
    gate.setGate(c.confGate);
    fInit = c.baseFixed ? 2.0 * c.baseFreqHz * std::sqrt(2.0) : c.rangeHiHz;
}

void Divide::process(std::span<const double> source, std::span<const double>, std::span<double> out)
{
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = processSample(i < source.size() ? source[i] : 0.0);
}

void Divide::insertEvent(const QueuedEvent& e)
{
    if (queueCount == static_cast<int>(queue.size()))
        return; // cannot happen: the 50 ms lockout keeps a few events within the lookahead
    int pos = queueCount;
    while (pos > 0 && queue[static_cast<size_t>(pos - 1)].sample > e.sample)
    {
        queue[static_cast<size_t>(pos)] = queue[static_cast<size_t>(pos - 1)];
        --pos;
    }
    queue[static_cast<size_t>(pos)] = e;
    ++queueCount;
}

SampleEvents Divide::eventsFor(std::int64_t sample, bool consume)
{
    SampleEvents ev;
    int keep = 0;
    for (int i = 0; i < queueCount; ++i)
    {
        const auto& e = queue[static_cast<size_t>(i)];
        if (e.sample == sample)
        {
            if (e.isEnd)
                ev.end = true;
            else
            {
                (e.after ? ev.startAfter : ev.startBefore) = true;
                ev.ramp = e.ramp;
                ev.startTime = e.time;
            }
            if (consume)
                continue;
        }
        queue[static_cast<size_t>(keep++)] = e;
    }
    queueCount = keep;
    return ev;
}

void Divide::scheduleStart(std::int64_t cur, double detect, double begin)
{
    if (look == 0) // Live: at the detection, with a ramp; a detection inside the sample comes before its crossing
    {
        const auto k = static_cast<std::int64_t>(std::ceil(detect));
        insertEvent({k, !(detect < static_cast<double>(k)), false, true, detect});
        return;
    }
    // Mix/Print: the last zero crossing in [ceil(detect) - E, max(begin, detect - E)]; without one, max(begin,
    // ceil(detect) - E) (the prototype takes floor(max(begin, detect - E)), a sample earlier when begin < detect - E)
    const double p = std::max(begin, detect - look);
    const std::int64_t lo = static_cast<std::int64_t>(std::ceil(detect)) - look;
    std::int64_t k = -1;
    for (auto c = static_cast<std::int64_t>(std::floor(p)); c >= std::max<std::int64_t>(lo, 1) && c <= cur; --c)
    {
        const double a = ringX(c - 1);
        const double b = ringX(c);
        if ((a < 0.0 && b >= 0.0) || (a > 0.0 && b <= 0.0))
        {
            k = c;
            break;
        }
    }
    if (k < 0)
        k = std::max(static_cast<std::int64_t>(std::floor(p)), lo);
    insertEvent({std::max<std::int64_t>(k, 0), true, false, false, p});
}

bool Divide::advanceCurrentAxis(std::int64_t cur, double xb)
{
    activity.process(xb);
    const NoteEvent ev = notes.process(xb, activity.active());
    const double theta = std::max((look == 0 ? 0.03 : 0.05) * activity.ePeak(), kArmingFloor);
    ringXb[static_cast<size_t>(cur) % ringXb.size()] = xb;
    ringTheta[static_cast<size_t>(cur) % ringTheta.size()] = theta;
    if (ev.end)
        insertEvent({cur, true, true, false, static_cast<double>(cur)});
    if (ev.start)
        scheduleStart(cur, ev.detect, ev.begin);
    return ev.start;
}

void Divide::applySignOp(SignOp op, std::int64_t at)
{
    if (at < signs.rampEnd) // during a ramp an operation waits for its end (the prototype's max(i, p))
    {
        if (signs.deferredCount < static_cast<int>(signs.deferred.size()))
            signs.deferred[static_cast<size_t>(signs.deferredCount++)] = op;
        return;
    }
    switch (op)
    {
    case SignOp::Flip1:
        signs.cur1 = -signs.cur1;
        break;
    case SignOp::Flip2:
        signs.cur2 = -signs.cur2;
        break;
    case SignOp::Switch:
        signs.cur1 = signs.cur2 = 1.0;
        break;
    case SignOp::Ramp:
        signs.from1 = signs.cur1;
        signs.from2 = signs.cur2;
        signs.rampStart = at;
        signs.rampEnd = at + rampLength;
        signs.cur1 = signs.cur2 = 1.0;
        break;
    }
}

void Divide::currentSigns(std::int64_t at, double& s1, double& s2)
{
    if (at < signs.rampEnd)
    {
        const double frac = static_cast<double>(at - signs.rampStart + 1) / rampLength;
        s1 = signs.from1 + (1.0 - signs.from1) * frac;
        s2 = signs.from2 + (1.0 - signs.from2) * frac;
        return;
    }
    s1 = signs.cur1;
    s2 = signs.cur2;
}

double Divide::smooth(Smoother& s, double y, double cutoff)
{
    const double u = std::log(cutoff);
    if (!s.initialised)
    {
        s.slewState = (1.0 - slewCoeff) * u;
        s.initialised = true;
    }
    const double logFc = slewCoeff * u + s.slewState; // one-pole slew over 5 ms in the log domain
    s.slewState = (1.0 - slewCoeff) * logFc;
    const double g = std::tan(std::numbers::pi * std::exp(logFc) / fs);
    s.a.set(dsp::makeSvfSection(g, kSection1, 0.0, 0.0, 1.0));
    s.b.set(dsp::makeSvfSection(g, kSection2, 0.0, 0.0, 1.0));
    return s.b.process(s.a.process(y));
}

double Divide::processSample(double source)
{
    const std::int64_t cur = n++;
    const double xb = band.process(source);
    const bool startNow = advanceCurrentAxis(cur, xb);

    std::array<SignOp, 4> ops{};
    const std::int64_t m = cur - look;
    DivideProbe p;
    double y1 = 0.0;
    double y2 = 0.0;
    if (m >= 0)
    {
        // delayed axis: core A, then the flip-flop signs of sample m (operations held back by a ramp come first)
        while (signs.deferredCount > 0 && m >= signs.rampEnd)
        {
            const auto list = signs.deferred;
            const int count = signs.deferredCount;
            const std::int64_t at = signs.rampEnd;
            signs.deferredCount = 0;
            for (int i = 0; i < count; ++i)
                applySignOp(list[static_cast<size_t>(i)], at);
        }
        const int count = coreA.step(ringX(m), ringT(m), eventsFor(m, true), ops);
        for (int i = 0; i < count; ++i)
            applySignOp(ops[static_cast<size_t>(i)], m);
        double s1 = 1.0;
        double s2 = 1.0;
        currentSigns(m, s1, s2);
        y1 = ringX(m) * s1;
        y2 = y1 * s2;
        p.y1 = y1;
        p.y2 = y2;
        p.fEst = coreA.fEst();
        p.conf = coreA.conf();
        p.provisional = coreA.isProvisional();
        p.reset = coreA.resetNow;
    }

    // current axis: the confidence as known now. Core B is re-derived at a note start: core A advanced over the
    // samples it has not reached yet, with every event known now. In Live core A is on the current axis itself.
    double confKnown = 0.0;
    bool provKnown = false;
    if (look == 0)
    {
        confKnown = coreA.conf();
        provKnown = coreA.isProvisional();
    }
    else
    {
        if (startNow)
        {
            coreB = coreA;
            for (std::int64_t j = coreA.next; j <= cur; ++j)
                coreB.step(ringX(j), ringT(j), eventsFor(j, false), ops);
        }
        else
            coreB.step(ringX(cur), ringT(cur), eventsFor(cur, false), ops);
        confKnown = coreB.conf();
        provKnown = coreB.isProvisional();
    }
    lastConfKnown = confKnown;
    if (m < 0)
    {
        lastProbe = p;
        return 0.0;
    }

    // tracking low-pass: f_init from every reset until the note's first estimate, then the last estimate
    if (coreA.resetNow)
        fHeld = coreA.fEst() > 0.0 ? coreA.fEst() : fInit;
    else if (coreA.fEst() > 0.0)
        fHeld = coreA.fEst();
    const double shift = std::pow(2.0, 5.0 * control.shape);
    const double cutoff1 = std::min(0.87 * fHeld * 1.0 * shift, 0.45 * fs);
    const double cutoff2 = std::min(0.87 * fHeld * 0.5 * shift, 0.45 * fs);
    const double sm1 = smooth(smooth1, y1, cutoff1);
    const double sm2 = smooth(smooth2, y2, cutoff2);

    // Constant: divided by the sliding peak of |x_b| over 1.25 source periods (range_lo before the note's estimate)
    if (coreA.resetNow)
        fConstant = 0.0;
    if (coreA.fEst() > 0.0)
        fConstant = coreA.fEst();
    const double fRef =
        fConstant > 0.0 ? std::clamp(fConstant, control.rangeLoHz, control.rangeHiHz) : control.rangeLoHz;
    constantPeak.setWindow(std::max(1, static_cast<int>(std::ceil(1.25 * fs / fRef))));
    const double peak = constantPeak.process(std::abs(ringX(m)));
    const double constantScale = peak > 1e-6 ? kConstantLevel / peak : 0.0;
    const double scale = control.follow ? 1.0 : constantScale;

    const double gain = gate.process(confKnown, provKnown);
    p.cutoffTarget = cutoff1;
    p.smoothed = sm1;
    p.constantScale = scale;
    lastProbe = p;
    lastFEst = coreA.fEst();
    return (control.sub1Gain * sm1 + control.sub2Gain * sm2) * gain * scale;
}
} // namespace subgen::engines
