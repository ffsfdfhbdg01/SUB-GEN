// SPDX-License-Identifier: AGPL-3.0-or-later
#include "chain/SubFilters.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace subgen::chain
{
namespace
{
constexpr double kSmoothMs = 20.0;
constexpr double kFadeMs = 20.0;
constexpr double kWarmMs = 50.0;
constexpr double kPhaseFadeMs = 40.0;
constexpr int kLfeHpOrder = 4; // lfe_hp_freq: 24 dB/oct (the registry has no LFE high-pass slope)

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double finiteOr(double v, double fallback, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }

// the nearest of 12, 24, 48, 96 dB/oct as a filter order; Bessel stops at 8 (48 dB/oct)
int orderOf(int slope, dsp::FilterFamily family)
{
    const int n = slope <= 18 ? 2 : slope <= 36 ? 4 : slope <= 72 ? 8 : 16;
    return family == dsp::FilterFamily::Bessel ? std::min(n, 8) : n;
}

dsp::FilterFamily familyOf(dsp::FilterFamily family) // a stray value would design nothing
{
    return family == dsp::FilterFamily::LinkwitzRiley || family == dsp::FilterFamily::Bessel ? family
                                                                                            : dsp::FilterFamily::Butterworth;
}
} // namespace

LowPassDesign lowPassOf(const SubFilterParams& p, double fs)
{
    LowPassDesign d;
    d.family = familyOf(p.family);
    d.order = orderOf(p.lfeMode ? p.lfeLpSlope : p.lpSlope, d.family);
    d.fcHz = p.lfeMode ? finiteOr(p.lfeLpHz, 120.0, 5.0, 0.45 * fs) : finiteOr(p.lpHz, 140.0, 5.0, 0.45 * fs);
    return d;
}

void SubFilters::Slot::follow(double hpHz, double lpHz, double fs)
{
    if (hpHz != madeHp)
    {
        hp.set(dsp::designFilter(shape.family, dsp::FilterKind::HighPass, shape.hpOrder, hpHz, fs));
        madeHp = hpHz;
    }
    if (lpHz != madeLp)
    {
        lp.set(dsp::designFilter(shape.family, dsp::FilterKind::LowPass, shape.lpOrder, lpHz, fs));
        madeLp = lpHz;
    }
}

void SubFilters::prepare(double sampleRate, int maxBlock, Quality quality)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    history.assign(static_cast<size_t>(std::max(1, msToSamples(kWarmMs, fs))), 0.0);
    linearPrepared = quality == Quality::Print;
    if (linearPrepared)
    {
        linear.prepare(fs, maxBlock);
        highPassed.assign(static_cast<size_t>(std::max(maxBlock, 1)), 0.0);
    }
    else
    {
        linear.release();
        highPassed.clear();
    }
    reset();
}

void SubFilters::reset()
{
    for (auto& s : slots)
    {
        s.hp.reset();
        s.lp.reset();
        s.madeHp = s.madeLp = -1.0;
    }
    std::fill(history.begin(), history.end(), 0.0);
    written = 0;
    fading = false;
    linear.reset();
    linearWanted = linearRunning = false;
    fresh = true;
}

void SubFilters::setParams(const SubFilterParams& p)
{
    const LowPassDesign lp = lowPassOf(p, fs);
    Shape s;
    s.family = lp.family;
    s.lpOrder = lp.order;
    s.hpOrder = p.lfeMode ? kLfeHpOrder : orderOf(p.hpSlope, s.family);
    hpTarget = p.lfeMode ? finiteOr(p.lfeHpHz, 20.0, 5.0, 0.45 * fs) : finiteOr(p.hpHz, 22.0, 5.0, 0.45 * fs);
    lpTarget = lp.fcHz;
    requested = s;
    const bool wanted = linearPrepared && p.linearPhase;
    if (wanted)
        linear.requestDesign(p);
    if (fresh)
    {
        fresh = false;
        hpCutoff.reset(fs, kSmoothMs, hpTarget);
        lpCutoff.reset(fs, kSmoothMs, lpTarget);
        fade.reset(fs, kFadeMs, 1.0);
        Slot& a = slots[active];
        a.shape = s;
        a.madeHp = a.madeLp = -1.0;
        a.follow(hpTarget, lpTarget, fs);
        linearWanted = linearRunning = wanted;
        phaseFade.reset(fs, kPhaseFadeMs, wanted ? 1.0 : 0.0);
        return;
    }
    hpCutoff.setTarget(hpTarget);
    lpCutoff.setTarget(lpTarget);
    if (wanted != linearWanted)
    {
        linearWanted = wanted;
        phaseFade.setTarget(wanted ? 1.0 : 0.0);
        if (wanted && !linearRunning) // a cold start: the history of an earlier Linear Phase spell is stale
        {
            linear.reset();
            linearRunning = true;
        }
    }
}

void SubFilters::startChange(double hpHz, double lpHz)
{
    const size_t incoming = active ^ 1;
    Slot& s = slots[incoming];
    s.shape = requested;
    s.hp.reset();
    s.lp.reset();
    s.madeHp = s.madeLp = -1.0;
    s.follow(hpHz, lpHz, fs);
    for (size_t i = 0; i < history.size(); ++i) // warm-up, the oldest sample first
        (void)s.process(history[(written + i) % history.size()]);
    active = incoming;
    fade.reset(fs, kFadeMs, 0.0);
    fade.setTarget(1.0);
    fading = true;
}

void SubFilters::filter(std::span<double> block, std::span<double> highPassedOut)
{
    for (size_t i = 0; i < block.size(); ++i)
    {
        const double x = block[i];
        const double h = hpCutoff.next();
        const double l = lpCutoff.next();
        if (!fading && requested != slots[active].shape)
            startChange(h, l);
        Slot& a = slots[active];
        a.follow(h, l, fs);
        double hp = a.hp.process(x);
        double y = a.lp.process(hp);
        if (fading)
        {
            Slot& o = slots[active ^ 1];
            o.follow(h, l, fs);
            const double oh = o.hp.process(x);
            const double w = sCurve(fade.next());
            y = w * y + (1.0 - w) * o.lp.process(oh);
            hp = w * hp + (1.0 - w) * oh;
            fading = fade.isSmoothing();
        }
        history[written] = x;
        written = written + 1 == history.size() ? 0 : written + 1;
        if (!highPassedOut.empty())
            highPassedOut[i] = hp;
        block[i] = y;
    }
}

void SubFilters::process(std::span<double> block)
{
    if (fresh)
        return;
    if (!linearRunning)
    {
        filter(block, {});
        return;
    }
    for (size_t done = 0; done < block.size();)
    {
        const size_t n = std::min(block.size() - done, highPassed.size());
        const auto chunk = block.subspan(done, n);
        const auto hp = std::span<double>(highPassed).subspan(0, n);
        filter(chunk, hp); // chunk: minimum phase; hp: the high-pass alone
        linear.process(hp);
        for (size_t i = 0; i < n; ++i)
        {
            const double w = sCurve(phaseFade.next());
            chunk[i] = w == 1.0 ? hp[i] : (w == 0.0 ? chunk[i] : w * hp[i] + (1.0 - w) * chunk[i]);
        }
        done += n;
    }
    if (!linearWanted && !phaseFade.isSmoothing())
        linearRunning = false; // back to minimum phase
}

double SubFilters::groupDelayMs(double fHz) const
{
    dsp::FilterCascade hp; // the target design, wherever the smoothing is
    hp.set(dsp::designFilter(requested.family, dsp::FilterKind::HighPass, requested.hpOrder, hpTarget, fs));
    double seconds = hp.groupDelaySeconds(fHz, fs);
    if (!linearWanted) // the Linear Phase kernel's delay is latency, compensated with the dry path
    {
        dsp::FilterCascade lp;
        lp.set(dsp::designFilter(requested.family, dsp::FilterKind::LowPass, requested.lpOrder, lpTarget, fs));
        seconds += lp.groupDelaySeconds(fHz, fs);
    }
    return 1000.0 * seconds;
}
} // namespace subgen::chain
