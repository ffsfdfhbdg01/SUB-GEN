// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engines/Track.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "core/Quality.h"

namespace subgen::engines
{
namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;
const double kSemitone = std::log(2.0) / 12.0;
constexpr double kNarrowHz = 5.0;
constexpr double kWideHz = 20.0;
// rad: the loop is losing the source (a glide in Live, before the frames show it). Low-passed, the phase error of a
// 5 Hz loop at in-band SNR 10 dB has ~0.1 rad rms: it widens above 0.45 rad (rare in noise) and stays wide while
// the error exceeds 0.15 rad (a glide keeps it there even at 20 Hz).
constexpr double kLockOn = 0.45;
constexpr double kLockOff = 0.15;

double wrapTwoPi(double x) { return x - kTwoPi * std::floor(x / kTwoPi); }
double wrapPi(double x) { return x - kTwoPi * std::floor((x + std::numbers::pi) / kTwoPi); }

// (1 - shape) sin(phi) + shape tri(phi), tri = (2 / pi) asin(sin phi) as a piecewise line (phi in [0, 2 pi))
double wave(double phi, double shape)
{
    const double u = phi * (2.0 / std::numbers::pi);
    const double tri = u < 1.0 ? u : (u < 3.0 ? 2.0 - u : u - 4.0);
    return (1.0 - shape) * std::sin(phi) + shape * tri;
}
} // namespace

void Track::prepare(double sampleRate, int, int lookahead)
{
    fs = sampleRate;
    look = std::max(0, lookahead);
    band.prepare(fs);
    activity.prepare(fs);
    notes.prepare(fs);
    onsets.prepare(fs);
    gate.prepare(fs);
    shaper.prepare(fs);
    peakHold.prepare(static_cast<int>(std::ceil(1.5 * fs / 15.0)) + 1);
    mpm.prepare(fs, 300.0);
    pll.prepare(fs);
    frames.assign(static_cast<size_t>(mpm.maxFrames(1)), PitchFrame{});
    history = look + static_cast<int>(std::ceil(0.25 * fs));
    xbRing.assign(static_cast<size_t>(history) + 1, 0.0);
    ampRing.assign(static_cast<size_t>(look) + 1, 0.0);
    glideFrames = std::max(1, static_cast<int>(std::lround(0.020 * fs / mpm.hopSamples())));
    frameLogF.assign(static_cast<size_t>(glideFrames) + 1, 0.0);
    wideHold = msToSamples(20.0, fs);
    pllAmpCoeff = 1.0 - std::exp(-1.0 / (0.05 * fs));
    voiceCoeff = 1.0 - std::exp(-1.0 / (0.001 * fs));
    lockCoeff = 1.0 - std::exp(-1.0 / (0.01 * fs));
    setControl(control);
    reset();
}

void Track::reset()
{
    band.reset();
    activity.reset();
    notes.reset();
    onsets.reset();
    onsetCount = 0;
    lastOnset = std::numeric_limits<double>::quiet_NaN();
    gate.reset();
    shaper.reset();
    peakHold.reset();
    mpm.reset();
    prior.reset();
    pll.setBandwidth(kNarrowHz);
    pll.seed(60.0, 0.0);
    std::fill(xbRing.begin(), xbRing.end(), 0.0);
    std::fill(ampRing.begin(), ampRing.end(), 0.0);
    std::fill(frameLogF.begin(), frameLogF.end(), std::numeric_limits<double>::quiet_NaN());
    n = 0;
    frameCount = 0;
    latestF0 = 0.0;
    latestConf = 0.0;
    latestTracking = false;
    wideUntil = -1;
    wide = false;
    pendingBegin = -1;
    noteBegin = -1;
    seedPending = false;
    pllSeeded = false;
    mismatchSamples = 0;
    pllA2 = 0.0;
    lockError1 = lockError2 = 0.0;
    lockLost = false;
    reportBegin = -1;
    reportedDelayMs = 0.0;
    wasTracking = false;
    sounding = false;
    voice = 0.0;
    heldF1 = 0.0;
    glideLogF = speedLogF = lastLogF = 0.0;
    rampLeft = 0;
    phase1 = phase2 = 0.0;
    lastProbe = TrackProbe{};
}

void Track::setControl(const EngineControl& requested)
{
    const EngineControl c = clampControl(requested);
    control = c;
    band.setRange(c.rangeLoHz, c.rangeHiHz, c.rangeSlope);
    activity.setThreshold(c.detThreshDb);
    notes.setThreshold(c.detThreshDb);
    onsets.set(c.trigThreshDb, c.trigHoldMs, c.trigLockoutMs);
    gate.setGate(c.confGate);
    shaper.set(c.follow, c.envAttackMs, c.envReleaseMs, c.dynamics);
    peakHold.setWindow(static_cast<int>(std::ceil(1.5 * fs / c.rangeLoHz)));
    mpm.setMinFrequency(c.rangeLoHz);
    const double tauMs = 80.0 * std::pow(5.0 / 80.0, c.trkSpeed);
    speedCoeff = 1.0 - std::exp(-1.0 / (tauMs * 1e-3 * fs));
    glideSamples = msToSamples(c.trkGlideMs, fs);
}

void Track::process(std::span<const double> source, std::span<const double> key, std::span<double> out)
{
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = processSample(i < source.size() ? source[i] : 0.0, i < key.size() ? key[i] : 0.0);
}

// Current axis: activity, note starts, onsets, the amplitude (delayed by E for the output) and the pitch frames.
void Track::analyse(std::int64_t cur, double xb, double key)
{
    if (const auto offset = onsets.process(control.keyMode == KeyMode::Trigger ? key : xb))
        if (onsetCount < static_cast<int>(onsetQueue.size()))
            onsetQueue[static_cast<size_t>(onsetCount++)] = static_cast<double>(cur) + *offset;
    const double mute = activity.process(xb);
    const NoteEvent ev = notes.process(xb, activity.active());
    const double amp = shaper.process(peakHold.process(std::abs(xb)), activity.ePeak(), activity.eRms(), ev.start);
    xbRing[static_cast<size_t>(cur) % xbRing.size()] = xb;
    ampRing[static_cast<size_t>(cur) % ampRing.size()] = amp * mute;
    if (ev.start)
        pendingBegin = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(ev.begin)));
    if (mpm.process(std::span<const double>(&xb, 1), frames) == 0)
        return;
    latestF0 = prior.update(frames[0].f0Hz, frames[0].confidence);
    latestConf = frames[0].confidence;
    latestTracking = prior.tracking();
    // the PLL widens while the pitch moves more than a semitone in 20 ms of frames
    const double logF = latestTracking ? std::log(latestF0) : std::numeric_limits<double>::quiet_NaN();
    frameLogF[static_cast<size_t>(frameCount) % frameLogF.size()] = logF;
    if (frameCount >= glideFrames)
    {
        const double old = frameLogF[static_cast<size_t>(frameCount - glideFrames) % frameLogF.size()];
        if (std::isfinite(old) && std::isfinite(logF) && std::abs(logF - old) > kSemitone)
            wideUntil = cur + wideHold;
    }
    ++frameCount;
}

// Seeds the PLL with f0 and a phase from the note's first half-wave: its sign and the zero crossing it starts at,
// searched from the note's start (or a period before m without one) over 1.5 periods of what has been seen. The
// crossing has phase -pi/2 (rising) or +pi/2 (falling) in x ~ cos(phi); there the sub phase is phi / 2 + pi, so Sub -1
// starts with the half-wave's sign, and Sub -2 (phi / 4 + pi / 2, plus pi after a falling crossing) with it too.
void Track::seedPll(std::int64_t m, std::int64_t cur, double f0)
{
    const double period = fs / f0;
    const std::int64_t oldest = std::max<std::int64_t>(0, cur - history + 1);
    const std::int64_t from =
        std::max(oldest, noteBegin >= 0 ? noteBegin : m - static_cast<std::int64_t>(std::ceil(period)));
    const std::int64_t to = std::min(cur, from + static_cast<std::int64_t>(std::ceil(1.5 * period)));
    double peak = 0.0;
    for (std::int64_t j = from; j <= to; ++j)
        peak = std::max(peak, std::abs(ringX(j)));
    double phiSeed = 0.0;
    bool falling = false;
    if (peak > 0.0)
    {
        std::int64_t j = from;
        while (!(std::abs(ringX(j)) > 0.1 * peak))
            ++j;
        const double sign = ringX(j) > 0.0 ? 1.0 : -1.0;
        std::int64_t i = j - 1;
        while (i >= from && ringX(i) * sign > 0.0)
            --i;
        double crossing = static_cast<double>(from);
        if (i >= from)
        {
            const double a = ringX(i);
            const double b = ringX(i + 1);
            crossing = static_cast<double>(i) + (a == 0.0 ? 0.0 : a / (a - b));
        }
        falling = sign < 0.0;
        phiSeed = (falling ? 0.5 : -0.5) * std::numbers::pi + kTwoPi * f0 * (static_cast<double>(m) - crossing) / fs;
    }
    pll.seed(f0, phiSeed);
    phase1 = wrapTwoPi(0.5 * phiSeed + std::numbers::pi);
    phase2 = wrapTwoPi(0.25 * phiSeed + 0.5 * std::numbers::pi + (falling ? std::numbers::pi : 0.0));
}

// Free and Reset: the read pitch after the glide and the trk_speed smoothing, in log f. A new track (after none)
// starts at its value.
double Track::freeFrequency()
{
    const double logF = std::log(latestF0);
    if (!wasTracking)
    {
        glideLogF = speedLogF = lastLogF = logF;
        rampLeft = 0;
        return latestF0;
    }
    if (glideSamples > 0 && std::abs(logF - lastLogF) > kSemitone)
        rampLeft = glideSamples;
    if (rampLeft > 0)
        glideLogF += (logF - glideLogF) / rampLeft--;
    else
        glideLogF = logF;
    speedLogF += speedCoeff * (glideLogF - speedLogF);
    lastLogF = logF;
    return std::exp(speedLogF);
}

double Track::processSample(double source, double key)
{
    const std::int64_t cur = n++;
    analyse(cur, band.process(source), key);
    const std::int64_t m = cur - look;
    if (m < 0)
        return 0.0;

    // delayed axis
    const double xm = ringX(m);
    const double amplitude = ampRing[static_cast<size_t>(m) % ampRing.size()];
    if (pendingBegin >= 0 && m >= pendingBegin)
    {
        noteBegin = pendingBegin;
        pendingBegin = -1;
        seedPending = true;
        reportBegin = noteBegin;
    }
    TrackProbe p;
    double gateGain = 1.0;
    double f1 = 0.0;   // Sub -1 frequency
    double inc1 = 0.0; // phase advance to the next sample
    double inc2 = 0.0;
    if (control.baseFixed)
    {
        f1 = control.baseFreqHz;
        inc1 = kTwoPi * f1 / fs;
        inc2 = 0.5 * inc1;
    }
    else
    {
        // the gate reads the raw confidence, as the prototype: waiting for the prior's three frames would take 4 ms
        // of the lookahead from the gate's attack. The oscillator sounds from the first pitch of a track (a 1 ms
        // fade-in), keeps its last frequency while the gate releases after the track is lost, and stops once the gate
        // has closed.
        const bool valid = latestTracking;
        gateGain = gate.process(latestConf, false);
        if (valid)
            sounding = true;
        else if (gateGain < 1e-6)
            sounding = false;
        voice = sounding ? voice + voiceCoeff * (1.0 - voice) : 0.0;
        if (control.phaseGen == PhaseGen::Locked)
        {
            if (valid && (seedPending || !pllSeeded))
            {
                seedPll(m, cur, latestF0);
                seedPending = false;
                pllSeeded = true;
                mismatchSamples = 0;
            }
            // a legato note change starts no note: once the frames hold a pitch a semitone or more off the loop's for
            // 20 ms, the loop takes it (beyond the 20 Hz loop's pull-in range otherwise)
            else if (valid && pllSeeded && std::abs(std::log(latestF0 / std::max(1.0, pll.frequencyHz()))) >= kSemitone)
            {
                if (++mismatchSamples >= wideHold)
                {
                    pll.seed(latestF0, pll.phase()); // the new frequency; the loop keeps its phase and pulls it in
                    wideUntil = std::max(wideUntil, cur + wideHold);
                    mismatchSamples = 0;
                }
            }
            else
                mismatchSamples = 0;
            const bool w = cur < wideUntil;
            if (w != wide)
            {
                wide = w;
                pll.setBandwidth(wide ? kWideHz : kNarrowHz);
            }
            pllA2 += pllAmpCoeff * (2.0 * xm * xm - pllA2);
            const double now = pll.process(xm, std::sqrt(pllA2));
            lockError1 += lockCoeff * (pll.lastError() - lockError1);
            lockError2 += lockCoeff * (lockError1 - lockError2);
            const double lockError = std::abs(lockError2);
            lockLost = valid && (lockError > kLockOn || (lockLost && lockError > kLockOff));
            if (lockLost)
                wideUntil = std::max(wideUntil, cur + wideHold);
            const double inc = wrapPi(pll.phase() - now);
            inc1 = 0.5 * inc;
            inc2 = 0.25 * inc;
            f1 = sounding ? 0.5 * pll.frequencyHz() : 0.0;
        }
        else if (sounding)
        {
            if (valid)
                heldF1 = 0.5 * freeFrequency();
            f1 = heldF1;
            inc1 = kTwoPi * f1 / fs;
            inc2 = 0.5 * inc1;
        }
        wasTracking = valid;
        p.f0 = valid ? latestF0 : 0.0;
        p.pitchValid = valid;
        p.conf = latestConf;
        if (reportBegin >= 0 && m >= reportBegin && gate.open() && valid)
        {
            reportedDelayMs = 1000.0 * static_cast<double>(m - reportBegin) / fs;
            reportBegin = -1;
        }
    }
    // an onset reaching the delayed axis: Reset puts phase 0 at its fractional time
    if (onsetCount > 0 && onsetQueue[0] <= static_cast<double>(m))
    {
        lastOnset = onsetQueue[0];
        std::move(onsetQueue.begin() + 1, onsetQueue.begin() + onsetCount, onsetQueue.begin());
        --onsetCount;
        if (control.phaseGen == PhaseGen::Reset)
        {
            const double since = (static_cast<double>(m) - lastOnset) / fs;
            phase1 = wrapTwoPi(kTwoPi * f1 * since);
            phase2 = wrapTwoPi(std::numbers::pi * f1 * since);
        }
    }
    const double envelope = amplitude * gateGain * (control.baseFixed ? 1.0 : voice);
    const double y =
        (control.sub1Gain * wave(phase1, control.shape) + control.sub2Gain * wave(phase2, control.shape)) * envelope;
    p.gate = gateGain;
    p.envelope = envelope;
    p.ready = control.baseFixed || (sounding && gate.open());
    p.phase1 = phase1;
    p.subFreq = f1;
    lastProbe = p;
    phase1 = wrapTwoPi(phase1 + inc1);
    phase2 = wrapTwoPi(phase2 + inc2);
    return y;
}
} // namespace subgen::engines
