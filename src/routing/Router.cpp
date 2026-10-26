// SPDX-License-Identifier: AGPL-3.0-or-later
#include "routing/Router.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/Quality.h"
#include "core/SampleRate.h"

namespace subgen::routing
{
namespace
{
constexpr double kSmoothMs = 20.0;
constexpr double kPolarityMs = 5.0;
constexpr double kModeMs = 30.0;
constexpr double kMaxDelayMs = 30.0;

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double finiteGain(double g) { return std::isfinite(g) ? std::clamp(g, 0.0, 16.0) : 0.0; }

int lfeOrder(int slope) { return slope <= 18 ? 2 : slope <= 36 ? 4 : slope <= 72 ? 8 : 16; }
} // namespace

void Router::prepare(double sampleRate, [[maybe_unused]] int maxBlock, const ChannelMap& out)
{
    fs = sanitizeSampleRate(sampleRate);
    map = out;
    for (auto& d : delay)
        d.prepare(msToSamples(kMaxDelayMs, fs) + 1);
    reset();
}

void Router::reset()
{
    for (auto& d : delay)
        d.reset();
    lfeHp.reset();
    lfeLp.reset();
    modeFading = false;
    fresh = true;
}

OutMode Router::resolve(OutMode mode) const
{
    if (mode == OutMode::Auto)
        return map.hasLfe ? OutMode::Lfe : OutMode::Mono;
    return mode == OutMode::Lfe && !map.hasLfe ? OutMode::Mono : mode; // W-01
}

void Router::setParams(const RouterParams& p)
{
    params = p;
    const auto family = p.family == dsp::FilterFamily::LinkwitzRiley || p.family == dsp::FilterFamily::Bessel
                            ? p.family
                            : dsp::FilterFamily::Butterworth;
    const double hp = std::isfinite(p.lfeHpHz) ? std::clamp(p.lfeHpHz, 5.0, 0.45 * fs) : 20.0;
    const double lp = std::isfinite(p.lfeLpHz) ? std::clamp(p.lfeLpHz, 5.0, 0.45 * fs) : 120.0;
    lfeHp.set(dsp::designFilter(family, dsp::FilterKind::HighPass, 4, hp, fs));
    lfeLp.set(dsp::designFilter(family, dsp::FilterKind::LowPass, lfeOrder(p.lfeLpSlope), lp, fs));
    for (int c = 0; c < map.count; ++c)
    {
        const auto cls = map.ch[static_cast<size_t>(c)].cls;
        if (cls == ChannelClass::Other)
            continue;
        const double ms = p.classDelayMs[static_cast<size_t>(cls)];
        delay[static_cast<size_t>(c)].setDelay(msToSamples(std::isfinite(ms) ? std::clamp(ms, 0.0, kMaxDelayMs) : 0.0, fs));
    }
    const OutMode wanted = resolve(p.mode);
    const double w = std::isfinite(p.width) ? std::clamp(p.width, 0.0, 1.0) : 0.0;
    if (fresh)
    {
        fresh = false;
        for (size_t k = 0; k < kNumClasses; ++k)
        {
            gain[k].reset(fs, kSmoothMs, finiteGain(p.classGain[k]));
            polarity[k].reset(fs, kPolarityMs, p.classInvert[k] ? 1.0 : 0.0);
        }
        lfeTrim.reset(fs, kSmoothMs, finiteGain(p.lfeTrimGain));
        lfeBleed.reset(fs, kSmoothMs, finiteGain(p.lfeBleedGain));
        width.reset(fs, kSmoothMs, w);
        current = previous = wanted;
        return;
    }
    for (size_t k = 0; k < kNumClasses; ++k)
    {
        gain[k].setTarget(finiteGain(p.classGain[k]));
        polarity[k].setTarget(p.classInvert[k] ? 1.0 : 0.0);
    }
    lfeTrim.setTarget(finiteGain(p.lfeTrimGain));
    lfeBleed.setTarget(finiteGain(p.lfeBleedGain));
    width.setTarget(w);
}

double Router::feed(OutMode mode, const ChannelInfo& info, const Sources& s) const
{
    const auto k = static_cast<size_t>(info.cls);
    switch (mode)
    {
    case OutMode::Mono:
        return info.cls == ChannelClass::Front || info.cls == ChannelClass::Center ? gainNow[k] * s.mid : 0.0;
    case OutMode::Stereo:
        if (info.cls != ChannelClass::Front)
            return 0.0;
        return gainNow[k] * (info.side == Side::Right ? s.right : s.left);
    case OutMode::Surround:
        if (info.cls == ChannelClass::Lfe)
            return gainNow[k] * s.lfe;
        return gainNow[k] * (info.side == Side::Left ? s.left : (info.side == Side::Right ? s.right : s.centre));
    case OutMode::Lfe:
        if (info.cls == ChannelClass::Lfe)
            return trimNow * s.mid;
        return info.cls == ChannelClass::Front || info.cls == ChannelClass::Center ? bleedNow * trimNow * s.mid : 0.0;
    case OutMode::Auto:
        break;
    }
    return 0.0;
}

void Router::process(std::span<const double> s1, std::span<const double> s2, double* const* out, int n)
{
    const auto count = std::min(static_cast<size_t>(std::max(n, 0)), s1.size());
    const bool two = s2.size() >= count && !s2.empty();
    if (!modeFading && resolve(params.mode) != current)
    {
        previous = current;
        current = resolve(params.mode);
        modeFade.reset(fs, kModeMs, 0.0);
        modeFade.setTarget(1.0);
        modeFading = true;
    }
    for (size_t i = 0; i < count; ++i)
    {
        Sources s{};
        s.mid = two ? 0.5 * (s1[i] + s2[i]) : s1[i];
        const double w = width.next();
        s.left = two ? (1.0 - w) * s.mid + w * s1[i] : s1[i];
        s.right = two ? (1.0 - w) * s.mid + w * s2[i] : s1[i];
        s.centre = two ? 0.5 * (s.left + s.right) : s1[i];
        s.lfe = lfeLp.process(lfeHp.process(s.centre));
        for (size_t k = 0; k < kNumClasses; ++k)
            gainNow[k] = gain[k].next();
        trimNow = lfeTrim.next();
        bleedNow = lfeBleed.next();
        std::array<double, kNumClasses> pol{};
        for (size_t k = 0; k < kNumClasses; ++k)
        {
            const double u = polarity[k].next();
            pol[k] = u == 0.0 ? 1.0 : 1.0 - 2.0 * sCurve(u); // +1 ... -1 exactly at the ends
        }
        const double m = modeFading ? sCurve(modeFade.next()) : 1.0;
        for (int c = 0; c < map.count; ++c)
        {
            const auto& info = map.ch[static_cast<size_t>(c)];
            if (info.bufferIndex < 0)
                continue;
            double v = 0.0;
            if (info.cls != ChannelClass::Other)
            {
                v = feed(current, info, s);
                if (m != 1.0)
                    v = m * v + (1.0 - m) * feed(previous, info, s);
                v = pol[static_cast<size_t>(info.cls)] * delay[static_cast<size_t>(c)].process(v);
            }
            out[info.bufferIndex][i] = v;
        }
        if (modeFading)
            modeFading = modeFade.isSmoothing();
    }
    for (int c = 0; c < map.count; ++c) // a stream shorter than the block: the rest is silence
    {
        const int index = map.ch[static_cast<size_t>(c)].bufferIndex;
        if (index >= 0)
            for (size_t i = count; i < static_cast<size_t>(std::max(n, 0)); ++i)
                out[index][i] = 0.0;
    }
}
} // namespace subgen::routing
