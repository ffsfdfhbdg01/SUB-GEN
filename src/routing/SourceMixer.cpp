// SPDX-License-Identifier: AGPL-3.0-or-later
#include "routing/SourceMixer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/Quality.h"

namespace subgen::routing
{
namespace
{
constexpr double kSmoothMs = 20.0;
constexpr double kRmsMs = 200.0;
constexpr double kSwitchMs = 30.0;
constexpr double kHysteresis = 1.4125375446227544; // 1.5 dB in mean square

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

double finiteGain(double g) { return std::isfinite(g) ? std::clamp(g, 0.0, 16.0) : 0.0; }

bool inLeftGroup(Side s) { return s != Side::Right; }
bool inRightGroup(Side s) { return s != Side::Left; }
} // namespace

void SourceMixer::prepare(double sampleRate, int maxBlock, const ChannelMap& in)
{
    fs = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    map = in;
    hasLeft = hasRight = false;
    for (int c = 0; c < map.count; ++c)
    {
        const auto& info = map.ch[static_cast<size_t>(c)];
        if (info.cls == ChannelClass::Other)
            continue;
        hasLeft = hasLeft || info.side == Side::Left;
        hasRight = hasRight || info.side == Side::Right;
    }
    const int window = std::max(1, msToSamples(kRmsMs, fs));
    msLeft.prepare(window);
    msLeft.setWindow(window);
    msRight.prepare(window);
    msRight.setWindow(window);
    bandLeft.prepare(fs);
    bandRight.prepare(fs);
    corr.prepare(fs);
    key.assign(static_cast<size_t>(std::max(maxBlock, 1)), 0.0);
    reset();
}

void SourceMixer::reset()
{
    msLeft.reset();
    msRight.reset();
    bandLeft.reset();
    bandRight.reset();
    corr.reset();
    selected = 0;
    switching = false;
    keyLength = 0;
    fresh = true;
}

void SourceMixer::setParams(const SourceParams& p)
{
    params = p;
    const double lo = std::isfinite(p.bandLoHz) ? std::clamp(p.bandLoHz, 5.0, 0.4 * fs) : 30.0;
    const double hi = std::isfinite(p.bandHiHz) ? std::clamp(p.bandHiHz, lo, 0.45 * fs) : 120.0;
    bandLeft.setRange(lo, hi, 24);
    bandRight.setRange(lo, hi, 24);
    if (fresh)
    {
        fresh = false;
        for (size_t k = 0; k < feed.size(); ++k)
            feed[k].reset(fs, kSmoothMs, finiteGain(p.feedGain[k]));
        srcGain.reset(fs, kSmoothMs, finiteGain(p.srcGain));
        keyGain.reset(fs, kSmoothMs, finiteGain(p.keyGain));
        return;
    }
    for (size_t k = 0; k < feed.size(); ++k)
        feed[k].setTarget(finiteGain(p.feedGain[k]));
    srcGain.setTarget(finiteGain(p.srcGain));
    keyGain.setTarget(finiteGain(p.keyGain));
}

int SourceMixer::numStreams() const
{
    return params.mode == SourceMode::PerChannel && params.keyMode != KeyMode::Detect && hasLeft && hasRight ? 2 : 1;
}

void SourceMixer::process(const double* const* in, int n, const double* const* keyIn, int keyCh, std::span<double> sL,
                          std::span<double> sR)
{
    const size_t count = std::min({static_cast<size_t>(std::max(n, 0)), sL.size(), sR.size()});
    keyLength = 0;
    if (params.keyMode != KeyMode::Off && keyIn != nullptr && keyCh > 0)
    {
        keyLength = std::min(count, key.size());
        for (size_t i = 0; i < keyLength; ++i)
        {
            double sum = 0.0;
            for (int c = 0; c < keyCh; ++c)
                sum += keyIn[c][i];
            key[i] = sum * keyGain.next();
        }
    }
    const bool detect = params.keyMode == KeyMode::Detect && keyLength > 0;
    const bool two = numStreams() == 2;
    for (size_t i = 0; i < count; ++i)
    {
        double left = 0.0, right = 0.0;
        if (detect)
            left = right = i < keyLength ? key[i] : 0.0;
        else if (map.count == 1)
            left = right = in[map.ch[0].bufferIndex][i] * srcGain.next();
        else
        {
            std::array<double, kNumClasses> w{};
            for (size_t k = 0; k < w.size(); ++k)
                w[k] = feed[k].next();
            double numL = 0.0, denL = 0.0, numR = 0.0, denR = 0.0;
            for (int c = 0; c < map.count; ++c)
            {
                const auto& info = map.ch[static_cast<size_t>(c)];
                if (info.cls == ChannelClass::Other)
                    continue;
                const double weight = w[static_cast<size_t>(info.cls)];
                const double v = weight * in[info.bufferIndex][i];
                if (inLeftGroup(info.side))
                {
                    numL += v;
                    denL += weight;
                }
                if (inRightGroup(info.side))
                {
                    numR += v;
                    denR += weight;
                }
            }
            const double g = srcGain.next();
            left = denL > 0.0 ? g * (numL / denL) : 0.0;
            right = denR > 0.0 ? g * (numR / denR) : 0.0;
        }
        const double rho = corr.process(bandLeft.process(left), bandRight.process(right));
        // Max: the louder group, 1.5 dB of hysteresis, a 30 ms crossfade
        const double msL = msLeft.process(left), msR = msRight.process(right);
        if (!switching && (selected == 0 ? msR > kHysteresis * msL : msL > kHysteresis * msR))
        {
            selected ^= 1;
            switching = true;
            switchFade.reset(fs, kSwitchMs, 0.0);
            switchFade.setTarget(1.0);
        }
        double louder = selected == 0 ? left : right;
        if (switching)
        {
            const double w = sCurve(switchFade.next());
            louder = w * louder + (1.0 - w) * (selected == 0 ? right : left);
            switching = switchFade.isSmoothing();
        }
        const double sum = 0.5 * (left + right);
        double out = sum;
        switch (params.mode)
        {
        case SourceMode::Sum:
            break;
        case SourceMode::Max:
            out = louder;
            break;
        case SourceMode::PerChannel:
            if (two)
            {
                sL[i] = left;
                sR[i] = right;
                continue;
            }
            break;
        case SourceMode::Auto:
        {
            const double beta = std::clamp((0.25 - rho) / 0.5, 0.0, 1.0);
            out = beta == 0.0 ? sum : (beta == 1.0 ? louder : (1.0 - beta) * sum + beta * louder);
            break;
        }
        }
        if (map.count == 1 || detect)
            out = left; // one source: the stream is the source itself
        sL[i] = out;
        sR[i] = out;
    }
}
} // namespace subgen::routing
