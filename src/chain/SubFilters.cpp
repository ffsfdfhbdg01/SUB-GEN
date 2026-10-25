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
constexpr int kLfeHpOrder = 4; // lfe_hp_freq: 24 dB/oct (the registry has no LFE high-pass slope)

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double finiteOr(double v, double fallback, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }

// the nearest of 12, 24, 48, 96 dB/oct as a filter order; Bessel stops at 8 (48 dB/oct)
int orderOf(int slope, dsp::FilterFamily family)
{
    const int n = slope <= 18 ? 2 : slope <= 36 ? 4 : slope <= 72 ? 8 : 16;
    return family == dsp::FilterFamily::Bessel ? std::min(n, 8) : n;
}
} // namespace

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

// maxBlock and quality size Task 19's Linear Phase convolution
void SubFilters::prepare(double sampleRate, [[maybe_unused]] int maxBlock, [[maybe_unused]] Quality quality)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    history.assign(static_cast<size_t>(std::max(1, msToSamples(kWarmMs, fs))), 0.0);
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
    fresh = true;
}

void SubFilters::setParams(const SubFilterParams& p)
{
    Shape s;
    s.family = p.family == dsp::FilterFamily::LinkwitzRiley || p.family == dsp::FilterFamily::Bessel
                   ? p.family
                   : dsp::FilterFamily::Butterworth; // a stray value would design nothing
    if (p.lfeMode)
    {
        s.hpOrder = kLfeHpOrder;
        s.lpOrder = orderOf(p.lfeLpSlope, s.family);
        hpTarget = finiteOr(p.lfeHpHz, 20.0, 5.0, 0.45 * fs);
        lpTarget = finiteOr(p.lfeLpHz, 120.0, 5.0, 0.45 * fs);
    }
    else
    {
        s.hpOrder = orderOf(p.hpSlope, s.family);
        s.lpOrder = orderOf(p.lpSlope, s.family);
        hpTarget = finiteOr(p.hpHz, 22.0, 5.0, 0.45 * fs);
        lpTarget = finiteOr(p.lpHz, 140.0, 5.0, 0.45 * fs);
    }
    requested = s;
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
        return;
    }
    hpCutoff.setTarget(hpTarget);
    lpCutoff.setTarget(lpTarget);
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

void SubFilters::process(std::span<double> block)
{
    if (fresh)
        return;
    for (auto& x : block)
    {
        const double h = hpCutoff.next();
        const double l = lpCutoff.next();
        if (!fading && requested != slots[active].shape)
            startChange(h, l);
        Slot& a = slots[active];
        a.follow(h, l, fs);
        double y = a.process(x);
        if (fading)
        {
            Slot& o = slots[active ^ 1];
            o.follow(h, l, fs);
            const double w = sCurve(fade.next());
            y = w * y + (1.0 - w) * o.process(x);
            fading = fade.isSmoothing();
        }
        history[written] = x;
        written = written + 1 == history.size() ? 0 : written + 1;
        x = y;
    }
}

double SubFilters::groupDelayMs(double fHz) const
{
    dsp::FilterCascade hp, lp; // the target design, wherever the smoothing is
    hp.set(dsp::designFilter(requested.family, dsp::FilterKind::HighPass, requested.hpOrder, hpTarget, fs));
    lp.set(dsp::designFilter(requested.family, dsp::FilterKind::LowPass, requested.lpOrder, lpTarget, fs));
    return 1000.0 * (hp.groupDelaySeconds(fHz, fs) + lp.groupDelaySeconds(fHz, fs));
}
} // namespace subgen::chain
