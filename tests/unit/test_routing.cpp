// SPDX-License-Identifier: AGPL-3.0-or-later
// Routing (SPEC §5.1-5.2, §8.A, §9; SPEC_IO_UI §1-3): the channel classes and layouts, the source mixer S1/S2, the
// router S7 and the summation S8.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <numbers>
#include <numeric>
#include <vector>

#include "dsp/FilterCascade.h"
#include "dsp/FilterDesign.h"
#include "core/SampleRate.h"
#include "routing/ChannelMap.h"
#include "routing/Router.h"
#include "routing/SourceMixer.h"
#include "routing/Summation.h"
#include "support/AllocGuard.h"
#include "support/Measure.h"
#include "support/Signals.h"

using namespace subgen;
using namespace subgen::routing;
using Set = juce::AudioChannelSet;
namespace sig = subgen::test::signals;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 64;

using Channels = std::vector<std::vector<double>>;

ChannelMap mapOf(const Set& set)
{
    std::vector<int> order(static_cast<size_t>(set.size()));
    std::iota(order.begin(), order.end(), 0);
    return buildChannelMap(set, order, set.size());
}

// The amplitude of the tone at f in y[from, to): a least-squares fit of a cos + b sin.
double amplitudeAt(const std::vector<double>& y, double f, size_t from, size_t to)
{
    double cc = 0.0, ss = 0.0, cs = 0.0, yc = 0.0, ys = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * std::numbers::pi * f * static_cast<double>(i) / kFs;
        const double c = std::cos(w), s = std::sin(w);
        cc += c * c;
        ss += s * s;
        cs += c * s;
        yc += y[i] * c;
        ys += y[i] * s;
    }
    const double det = cc * ss - cs * cs;
    return std::hypot((yc * ss - ys * cs) / det, (ys * cc - yc * cs) / det);
}

double rmsDb(const std::vector<double>& y, size_t from, size_t to)
{
    return 20.0 * std::log10(std::max(test::rms(std::span<const double>(y.data() + from, to - from)), 1e-30));
}

// The source mixer over whole signals in blocks; the channels are in the map's buffer order.
struct Mixed
{
    std::vector<double> left, right;
    std::vector<int> selected; // per block (Max)
    double rho = 0.0;
    int streams = 0;
};

Mixed mix(const ChannelMap& map, const Channels& in, const SourceParams& p)
{
    const size_t n = in.front().size();
    SourceMixer mixer;
    mixer.prepare(kFs, kBlock, map);
    mixer.setParams(p);
    Mixed out;
    out.left.assign(n, 0.0);
    out.right.assign(n, 0.0);
    std::vector<const double*> ch(in.size());
    for (size_t i = 0; i < n; i += kBlock)
    {
        const int m = static_cast<int>(std::min<size_t>(kBlock, n - i));
        for (size_t c = 0; c < in.size(); ++c)
            ch[c] = in[c].data() + i;
        mixer.process(ch.data(), m, nullptr, 0, std::span<double>(out.left).subspan(i, static_cast<size_t>(m)),
                      std::span<double>(out.right).subspan(i, static_cast<size_t>(m)));
        out.selected.push_back(mixer.selectedChannel());
    }
    out.rho = mixer.correlation();
    out.streams = mixer.numStreams();
    return out;
}

// The router over whole signals; returns the output channels in the map's buffer order.
Channels route(const ChannelMap& map, const RouterParams& p, const std::vector<double>& s1, const std::vector<double>& s2)
{
    const size_t n = s1.size();
    Router router;
    router.prepare(kFs, kBlock, map);
    router.setParams(p);
    Channels out(static_cast<size_t>(map.count), std::vector<double>(n, 0.0));
    std::vector<double*> ch(out.size());
    for (size_t i = 0; i < n; i += kBlock)
    {
        const size_t m = std::min<size_t>(kBlock, n - i);
        for (size_t c = 0; c < out.size(); ++c)
            ch[c] = out[c].data() + i;
        router.process(std::span<const double>(s1).subspan(i, m),
                       s2.empty() ? std::span<const double>{} : std::span<const double>(s2).subspan(i, m), ch.data(),
                       static_cast<int>(m));
    }
    return out;
}

std::vector<double> impulse(size_t n)
{
    std::vector<double> x(n, 0.0);
    x[0] = 1.0;
    return x;
}

double maxFirstDifference(const std::vector<double>& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t>(from, 1); i < to; ++i)
        worst = std::max(worst, std::abs(y[i] - y[i - 1]));
    return worst;
}
} // namespace

TEST_CASE("supported layouts and pairs follow SPEC_IO_UI §2", "[routing]")
{
    for (const auto& set : {Set::mono(), Set::stereo(), Set::createLCR(), Set::createLRS(), Set::createLCRS(), Set::quadraphonic(),
                            Set::create5point0(), Set::create5point1(), Set::create6point0(), Set::create6point1(),
                            Set::create6point0Music(), Set::create6point1Music(), Set::create7point0(), Set::create7point1(),
                            Set::create7point0SDDS(), Set::create7point1SDDS(), Set::create5point0point2(),
                            Set::create5point1point2(), Set::create5point0point4(), Set::create5point1point4(),
                            Set::create7point0point2(), Set::create7point1point2(), Set::create7point0point4(),
                            Set::create7point1point4(), Set::create7point0point6(), Set::create7point1point6(),
                            Set::create9point0point4(), Set::create9point1point4(), Set::create9point0point6(),
                            Set::create9point1point6(), Set::create9point0point4ITU(), Set::create9point1point4ITU(),
                            Set::create9point0point6ITU(), Set::create9point1point6ITU(), Set::pentagonal(),
                            Set::hexagonal(), Set::octagonal()})
    {
        INFO(set.getDescription());
        CHECK(isSupportedLayout(set));
        CHECK(isSupportedPair(set, set));
    }
    CHECK_FALSE(isSupportedLayout(Set::ambisonic(1)));
    CHECK_FALSE(isSupportedLayout(Set::discreteChannels(4)));
    CHECK_FALSE(isSupportedLayout(Set::disabled()));
    // x.0 -> x.1, and mono -> stereo / 5.1 / 7.1 (wet only)
    CHECK(isSupportedPair(Set::create5point0(), Set::create5point1()));
    CHECK(isSupportedPair(Set::create7point0point4(), Set::create7point1point4()));
    CHECK(isSupportedPair(Set::create9point0point6(), Set::create9point1point6()));
    CHECK(isSupportedPair(Set::mono(), Set::stereo()));
    CHECK(isSupportedPair(Set::mono(), Set::create7point1()));
    CHECK_FALSE(isSupportedPair(Set::stereo(), Set::create5point1()));
    CHECK_FALSE(isSupportedPair(Set::create5point1(), Set::create5point0()));
    CHECK_FALSE(isSupportedPair(Set::create5point0(), Set::create7point1()));
    // the classes of SPEC_IO_UI §1
    const auto m = mapOf(Set::create7point1point4());
    CHECK(m.count == 12);
    CHECK(m.hasLfe);
    std::map<ChannelClass, int> perClass;
    for (int i = 0; i < m.count; ++i)
        ++perClass[m.ch[static_cast<size_t>(i)].cls];
    CHECK(perClass[ChannelClass::Front] == 2);
    CHECK(perClass[ChannelClass::Center] == 1);
    CHECK(perClass[ChannelClass::Surround] == 4);
    CHECK(perClass[ChannelClass::Height] == 4);
    CHECK(perClass[ChannelClass::Lfe] == 1);
    CHECK(mapOf(Set::mono()).ch[0].cls == ChannelClass::Center);
}

TEST_CASE("T-SRC-01 identical channels give unity +- 0.01 dB; mono passes; class weights apply", "[routing]")
{
    const auto x = sig::sine(50.0, kFs, 1.0, -12.0);
    SourceParams all;
    all.feedGain = {1.0, 1.0, 1.0, 1.0, 1.0};
    all.mode = SourceMode::PerChannel;
    const auto set = Set::create7point1point4();
    const auto same = mix(mapOf(set), Channels(static_cast<size_t>(set.size()), x), all);
    CHECK(std::abs(rmsDb(same.left, 4800, 48000) - rmsDb(x, 4800, 48000)) <= 0.01);
    CHECK(std::abs(rmsDb(same.right, 4800, 48000) - rmsDb(x, 4800, 48000)) <= 0.01);
    CHECK(same.streams == 2);
    // mono: the channel itself, whatever the Center feed
    SourceParams off;
    off.feedGain = {1.0, 0.0, 0.0, 0.0, 0.0};
    const auto mono = mix(mapOf(Set::mono()), Channels{x}, off);
    CHECK(mono.left == x);
    CHECK(mono.right == x);
    CHECK(mono.streams == 1);
    // weights on 5.1: Front 0 dB, Center -6 dB, Surround -12 dB, LFE Off; a tone per class
    const auto set51 = Set::create5point1();
    Channels in;
    const std::map<ChannelClass, double> tone{{ChannelClass::Front, 40.0}, {ChannelClass::Center, 60.0},
                                              {ChannelClass::Surround, 80.0}, {ChannelClass::Lfe, 100.0}};
    const auto map51 = mapOf(set51);
    for (int c = 0; c < set51.size(); ++c)
        in.push_back(sig::sine(tone.at(map51.ch[static_cast<size_t>(c)].cls), kFs, 1.0, -12.0));
    SourceParams weights;
    weights.feedGain = {1.0, 0.5, 0.25, 0.0, 0.0};
    weights.mode = SourceMode::PerChannel;
    const auto w = mix(map51, in, weights);
    const double a = amplitudeAt(x, 50.0, 4800, 48000);
    for (const auto& [f, share] : {std::pair{40.0, 1.0 / 1.75}, std::pair{60.0, 0.5 / 1.75}, std::pair{80.0, 0.25 / 1.75}})
    {
        INFO(f << " Hz");
        CHECK(std::abs(20.0 * std::log10(amplitudeAt(w.left, f, 4800, 48000) / (a * share))) <= 0.01);
        CHECK(std::abs(20.0 * std::log10(amplitudeAt(w.right, f, 4800, 48000) / (a * share))) <= 0.01);
    }
    CHECK(amplitudeAt(w.left, 100.0, 4800, 48000) <= 1e-9);
}

TEST_CASE("T-SRC-02 SIG-08: Auto >= -6 dB re the in-phase case; Sum flags W-03 (rho < -0.5)", "[routing]")
{
    const auto x = sig::sine(60.0, kFs, 2.0, -12.0);
    auto inverted = x;
    for (auto& v : inverted)
        v = -v;
    const auto map = mapOf(Set::stereo());
    SourceParams p;
    p.mode = SourceMode::Auto;
    const auto inPhase = mix(map, Channels{x, x}, p);
    const auto antiAuto = mix(map, Channels{x, inverted}, p);
    p.mode = SourceMode::Sum;
    const auto antiSum = mix(map, Channels{x, inverted}, p);
    INFO("Auto: in phase " << rmsDb(inPhase.left, 48000, 96000) << " dB, antiphase " << rmsDb(antiAuto.left, 48000, 96000)
                           << " dB; rho " << antiAuto.rho);
    CHECK(rmsDb(antiAuto.left, 48000, 96000) - rmsDb(inPhase.left, 48000, 96000) >= -6.0);
    CHECK(inPhase.rho > 0.9);
    CHECK(antiSum.rho < -0.5); // W-03 in Sum
    CHECK(rmsDb(antiSum.left, 48000, 96000) < -200.0);
}

TEST_CASE("the correlation reads the source band", "[routing]")
{
    // in-phase bass under louder antiphase highs: the full band correlates negatively, the source band positively, so
    // Auto sums
    const auto bass = sig::sine(50.0, kFs, 2.0, -18.0);
    const auto high = sig::sine(2000.0, kFs, 2.0, -6.0);
    std::vector<double> left(bass.size()), right(bass.size());
    for (size_t i = 0; i < bass.size(); ++i)
    {
        left[i] = bass[i] + high[i];
        right[i] = bass[i] - high[i];
    }
    SourceParams p;
    p.mode = SourceMode::Auto;
    const auto m = mix(mapOf(Set::stereo()), Channels{left, right}, p);
    INFO("rho " << m.rho);
    CHECK(m.rho > 0.9);
    CHECK(std::abs(20.0 * std::log10(amplitudeAt(m.left, 50.0, 48000, 96000) / amplitudeAt(bass, 50.0, 48000, 96000))) <= 0.01);
}

TEST_CASE("T-SRC-03 Max switches with 1.5 dB hysteresis and a 30 ms crossfade without flicker", "[routing]")
{
    const auto map = mapOf(Set::stereo());
    SourceParams p;
    p.mode = SourceMode::Max;
    // L 50 Hz at -12 dB; R 70 Hz 2 dB below, then 1.2 dB above (inside the hysteresis), then 2 dB above; level moves of
    // 10 ms
    const size_t n = 3 * 48000;
    const auto left = sig::sine(50.0, kFs, 3.0, -12.0);
    std::vector<double> right = sig::sine(70.0, kFs, 3.0, -12.0);
    auto levelAt = [](size_t i) {
        const double t = static_cast<double>(i) / kFs;
        auto ramp = [](double from, double to, double t0, double at) {
            return at < t0 ? from : (at > t0 + 0.01 ? to : from + (to - from) * (at - t0) / 0.01);
        };
        return t < 1.0 ? ramp(-2.0, 1.2, 0.99, t) : ramp(1.2, 2.0, 1.99, t);
    };
    for (size_t i = 0; i < n; ++i)
        right[i] *= std::pow(10.0, levelAt(i) / 20.0);
    const auto m = mix(map, Channels{left, right}, p);
    int switches = 0;
    size_t firstSwitchBlock = 0;
    for (size_t b = 1; b < m.selected.size(); ++b)
        if (m.selected[b] != m.selected[b - 1])
        {
            if (switches++ == 0)
                firstSwitchBlock = b;
        }
    const double switchTime = static_cast<double>(firstSwitchBlock * kBlock) / kFs;
    INFO("switches " << switches << ", the first at " << switchTime << " s");
    CHECK(switches == 1);
    CHECK(switchTime > 2.0); // not at +1.2 dB
    CHECK(switchTime < 2.25); // +2 dB: once the 200 ms RMS has followed
    CHECK(m.selected.back() == 1);
    // a 30 ms crossfade: 10 ms after the switch R is not yet all of it; 40 ms after, it is; no step
    const size_t at = firstSwitchBlock * kBlock;
    CHECK(amplitudeAt(m.left, 70.0, at, at + 480) < 0.8 * amplitudeAt(m.left, 70.0, at + 9600, at + 19200));
    CHECK(amplitudeAt(m.left, 50.0, at + 1920, at + 6720) <= 1e-9);
    const double steady = std::max(maxFirstDifference(m.left, at - 9600, at - 480), maxFirstDifference(m.left, at + 4800, at + 14400));
    CHECK(maxFirstDifference(m.left, at - 480, at + 2400) <= 1.2 * steady);
    // equal levels wavering by +-0.6 dB: no flicker
    std::vector<double> wavering = sig::sine(70.0, kFs, 3.0, -12.0);
    for (size_t i = 0; i < n; ++i)
        wavering[i] *= std::pow(10.0, 0.6 * std::sin(2.0 * std::numbers::pi * 2.0 * static_cast<double>(i) / kFs) / 20.0);
    const auto still = mix(map, Channels{left, wavering}, p);
    int changes = 0;
    for (size_t b = 1; b < still.selected.size(); ++b)
        changes += still.selected[b] != still.selected[b - 1] ? 1 : 0;
    CHECK(changes == 0);
}

TEST_CASE("T-SUR-01 class gains +- 0.01 dB, delays exact, polarity exact", "[routing]")
{
    const auto set = Set::create7point1point4();
    const auto map = mapOf(set);
    RouterParams p;
    p.mode = OutMode::Surround;
    p.classGain = {0.7, 0.5, 0.35, 0.25, 0.9};
    p.classDelayMs = {0.0, 1.0, 2.5, 7.0, 10.0};
    p.classInvert = {false, true, false, true, true};
    const size_t n = 4096;
    const auto u = route(map, p, impulse(n), {});
    dsp::FilterCascade hp, lp; // the LFE channel: the LFE filters after the sub's own low-pass
    hp.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::HighPass, 4, 20.0, kFs));
    lp.set(dsp::designFilter(dsp::FilterFamily::Butterworth, dsp::FilterKind::LowPass, 4, 120.0, kFs));
    std::vector<double> lfe(n);
    for (size_t i = 0; i < n; ++i)
        lfe[i] = lp.process(hp.process(i == 0 ? 1.0 : 0.0));
    for (int c = 0; c < map.count; ++c)
    {
        const auto cls = map.ch[static_cast<size_t>(c)].cls;
        const auto k = static_cast<size_t>(cls);
        const auto delay = static_cast<size_t>(std::lround(p.classDelayMs[k] * kFs / 1000.0));
        const double g = p.classGain[k] * (p.classInvert[k] ? -1.0 : 1.0);
        INFO("channel " << c << " class " << k);
        const auto& y = u[static_cast<size_t>(c)];
        if (cls == ChannelClass::Lfe)
        {
            double worst = 0.0;
            for (size_t i = 0; i < n; ++i)
                worst = std::max(worst, std::abs(y[i] - (i < delay ? 0.0 : g * lfe[i - delay])));
            CHECK(worst <= 1e-12);
            continue;
        }
        bool exact = true;
        for (size_t i = 0; i < n; ++i)
            exact = exact && y[i] == (i == delay ? g : 0.0);
        CHECK(exact);
    }
}

TEST_CASE("T-SUR-02 width 0 gives L = R, 100 independent, formula within 1e-9", "[routing]")
{
    const auto map = mapOf(Set::stereo());
    const auto s1 = sig::sine(40.0, kFs, 0.5, -12.0);
    const auto s2 = sig::sine(60.0, kFs, 0.5, -12.0);
    RouterParams p;
    p.mode = OutMode::Stereo;
    p.width = 0.0;
    const auto narrow = route(map, p, s1, s2);
    CHECK(narrow[0] == narrow[1]);
    p.width = 1.0;
    const auto wide = route(map, p, s1, s2);
    CHECK(wide[0] == s1);
    CHECK(wide[1] == s2);
    p.width = 0.3;
    const auto some = route(map, p, s1, s2);
    double worst = 0.0;
    for (size_t i = 0; i < s1.size(); ++i)
    {
        const double mid = 0.5 * (s1[i] + s2[i]);
        worst = std::max({worst, std::abs(some[0][i] - (0.7 * mid + 0.3 * s1[i])), std::abs(some[1][i] - (0.7 * mid + 0.3 * s2[i]))});
    }
    CHECK(worst <= 1e-9);
}

TEST_CASE("the router crossfades polarity over 5 ms and the output mode over 30 ms", "[routing]")
{
    // right after a change the output is still the old one (a hard switch: +6 dB off for the polarity)
    const auto set = Set::create5point1();
    const auto map = mapOf(set);
    const auto s = sig::sine(50.0, kFs, 0.5, -12.0);
    constexpr size_t kAt = 9600;
    auto render = [&](RouterParams p, bool change, auto edit) {
        Router router;
        router.prepare(kFs, kBlock, map);
        router.setParams(p);
        Channels out(static_cast<size_t>(map.count), std::vector<double>(s.size(), 0.0));
        std::vector<double*> ch(out.size());
        for (size_t i = 0; i < s.size(); i += kBlock)
        {
            if (change && i == kAt)
            {
                edit(p);
                router.setParams(p);
            }
            const size_t m = std::min<size_t>(kBlock, s.size() - i);
            for (size_t c = 0; c < out.size(); ++c)
                ch[c] = out[c].data() + i;
            router.process(std::span<const double>(s).subspan(i, m), {}, ch.data(), static_cast<int>(m));
        }
        return out;
    };
    RouterParams p;
    p.mode = OutMode::Surround;
    auto flip = [](RouterParams& q) { q.classInvert[0] = true; };
    const auto before = render(p, false, flip);
    const auto flipped = render(p, true, flip);
    const size_t front = 0; // JUCE 5.1: L R C LFE Ls Rs
    CHECK(test::nullDb(std::span<const double>(before[front]).subspan(kAt, 24), std::span<const double>(flipped[front]).subspan(kAt, 24)) <= -20.0);
    bool inverted = true;
    for (size_t i = kAt + 240; i < s.size(); ++i)
        inverted = inverted && flipped[front][i] == -before[front][i];
    CHECK(inverted);
    auto toMono = [](RouterParams& q) { q.mode = OutMode::Mono; };
    const auto moved = render(p, true, toMono);
    const size_t surround = 4;
    CHECK(test::nullDb(std::span<const double>(before[surround]).subspan(kAt, 48), std::span<const double>(moved[surround]).subspan(kAt, 48)) <= -20.0);
    CHECK(moved[surround].back() == 0.0); // Mono feeds Front and Center only
}

TEST_CASE("Auto output mode resolves to LFE with an LFE channel, else Mono", "[routing]")
{
    auto resolve = [](const Set& set, OutMode mode) {
        Router r;
        r.prepare(kFs, kBlock, mapOf(set));
        RouterParams p;
        p.mode = mode;
        r.setParams(p);
        return r.resolved();
    };
    CHECK(resolve(Set::create5point1(), OutMode::Auto) == OutMode::Lfe);
    CHECK(resolve(Set::create7point1point4(), OutMode::Auto) == OutMode::Lfe);
    CHECK(resolve(Set::stereo(), OutMode::Auto) == OutMode::Mono);
    CHECK(resolve(Set::create5point0(), OutMode::Auto) == OutMode::Mono);
    CHECK(resolve(Set::stereo(), OutMode::Lfe) == OutMode::Mono); // W-01: no LFE channel, as Mono
    CHECK(resolve(Set::create5point1(), OutMode::Surround) == OutMode::Surround);
}

TEST_CASE("Mono feeds Front and Center; LFE mode feeds the LFE channel at lfe_trim and the bleed", "[routing]")
{
    const auto set = Set::create5point1();
    const auto map = mapOf(set);
    const auto s = sig::sine(50.0, kFs, 0.5, -12.0);
    RouterParams p;
    p.mode = OutMode::Mono;
    p.classGain = {0.5, 0.25, 1.0, 1.0, 1.0};
    const auto mono = route(map, p, s, {});
    p.mode = OutMode::Lfe;
    p.lfeTrimGain = 0.8;
    p.lfeBleedGain = 0.1;
    const auto lfe = route(map, p, s, {});
    for (int c = 0; c < map.count; ++c)
    {
        const auto cls = map.ch[static_cast<size_t>(c)].cls;
        const auto& y = mono[static_cast<size_t>(c)];
        const auto& z = lfe[static_cast<size_t>(c)];
        INFO("channel " << c);
        const double monoGain = cls == ChannelClass::Front ? 0.5 : (cls == ChannelClass::Center ? 0.25 : 0.0);
        const double lfeGain = cls == ChannelClass::Lfe ? 0.8 : (cls == ChannelClass::Front || cls == ChannelClass::Center ? 0.08 : 0.0);
        bool exactMono = true, exactLfe = true;
        for (size_t i = 0; i < s.size(); ++i)
        {
            exactMono = exactMono && std::abs(y[i] - monoGain * s[i]) <= 1e-15;
            exactLfe = exactLfe && std::abs(z[i] - lfeGain * s[i]) <= 1e-15;
        }
        CHECK(exactMono);
        CHECK(exactLfe);
    }
}

TEST_CASE("T-SUM-02 mix 0 is bit-exact dry, 100 is sub only, equal power a^2 + b^2 = 1 within 1e-12", "[routing]")
{
    const auto dry = sig::sine(80.0, kFs, 0.2, -6.0);
    const auto sub = sig::sine(40.0, kFs, 0.2, -6.0);
    auto sum = [&](const SumParams& p, const std::vector<double>& d, const std::vector<double>& u) {
        Summation s;
        s.prepare(kFs);
        s.setParams(p);
        std::vector<double> out(d.size());
        for (size_t i = 0; i < d.size(); i += kBlock)
        {
            const int m = static_cast<int>(std::min<size_t>(kBlock, d.size() - i));
            const double* dp = d.data() + i;
            const double* up = u.data() + i;
            double* op = out.data() + i;
            s.process(&dp, &up, &op, 1, m);
        }
        return out;
    };
    SumParams p;
    p.mixMode = true;
    p.mix = 0.0;
    CHECK(sum(p, dry, sub) == dry);
    p.mix = 1.0;
    CHECK(sum(p, dry, sub) == sub);
    const std::vector<double> ones(512, 1.0), zeros(512, 0.0);
    for (const double m : {0.1, 0.25, 0.5, 0.8, 0.999})
    {
        p.mix = m;
        const double a = sum(p, ones, zeros).back(), b = sum(p, zeros, ones).back();
        INFO("mix " << m << ": a " << a << ", b " << b);
        CHECK(std::abs(a * a + b * b - 1.0) <= 1e-12);
        CHECK(std::abs(a - std::cos(std::numbers::pi * m / 2.0)) <= 1e-12);
    }
    p.equalPower = false;
    p.mix = 0.25;
    CHECK(std::abs(sum(p, ones, zeros).back() - 0.75) <= 1e-12);
    // Add: dry_level and the sub; Wet Only: the sub alone
    SumParams add;
    add.dryGain = 0.5;
    const auto added = sum(add, dry, sub);
    CHECK(std::abs(added[1000] - (0.5 * dry[1000] + sub[1000])) <= 1e-15);
    add.wetOnly = true;
    CHECK(sum(add, dry, sub) == sub);
    // a non-finite sub is zeroed before the sum (SPEC §9.3)
    auto broken = sub;
    broken[100] = std::numeric_limits<double>::quiet_NaN();
    SumParams plain;
    const auto guarded = sum(plain, dry, broken);
    CHECK(std::all_of(guarded.begin(), guarded.end(), [](double v) { return std::isfinite(v); }));
    CHECK(guarded[100] == dry[100]);
}

TEST_CASE("T-SUM-05 out_trim 0 dB is bit-exact", "[routing]")
{
    const auto dry = sig::bass808(kFs);
    std::vector<double> silent(dry.size(), 0.0);
    Summation s;
    s.prepare(kFs);
    SumParams p; // Add, dry 0 dB, out_trim 0 dB
    s.setParams(p);
    std::vector<double> out(dry.size());
    for (size_t i = 0; i < dry.size(); i += kBlock)
    {
        const int m = static_cast<int>(std::min<size_t>(kBlock, dry.size() - i));
        const double* dp = dry.data() + i;
        const double* up = silent.data() + i;
        double* op = out.data() + i;
        s.process(&dp, &up, &op, 1, m);
    }
    CHECK(out == dry);
}

TEST_CASE("Add and Mix crossfade over 20 ms without steps", "[routing]")
{
    // the switch lands off the dry's zero crossings (80 Hz has whole cycles at 9600): a hard switch would jump by the dry
    const auto dry = sig::sine(80.0, kFs, 0.5, -6.0);
    const auto sub = sig::sine(40.0, kFs, 0.5, -6.0);
    constexpr size_t kAt = 9664;
    auto render = [&](bool change) {
        Summation s;
        s.prepare(kFs);
        SumParams p;
        s.setParams(p);
        std::vector<double> out(dry.size());
        for (size_t i = 0; i < dry.size(); i += kBlock)
        {
            if (change && i == kAt)
            {
                p.mixMode = true;
                p.mix = 1.0;
                s.setParams(p);
            }
            const int m = static_cast<int>(std::min<size_t>(kBlock, dry.size() - i));
            const double* dp = dry.data() + i;
            const double* up = sub.data() + i;
            double* op = out.data() + i;
            s.process(&dp, &up, &op, 1, m);
        }
        return out;
    };
    const auto out = render(true), add = render(false);
    const double steady = std::max(maxFirstDifference(out, 4800, 9600), maxFirstDifference(out, 14400, 19200));
    CHECK(maxFirstDifference(out, kAt, kAt + 1920) <= 1.5 * steady);
    CHECK(test::nullDb(std::span<const double>(add).subspan(kAt, 48), std::span<const double>(out).subspan(kAt, 48)) <= -20.0);
    CHECK(out[12000] == sub[12000]); // after 20 ms: Mix 100 = the sub alone
}

TEST_CASE("T-IO-02 channel-order permutations (film, Logic, VST3, DTS) give identical per-class results", "[routing]")
{
    // 5.1 in four buffer orders; every channel type carries its own tone; per channel type the source streams and the
    // router's outputs must not depend on where the channel sits in the buffer
    const auto set = Set::create5point1();
    using T = Set::ChannelType;
    const std::vector<std::vector<T>> orders{
        {T::left, T::centre, T::right, T::leftSurround, T::rightSurround, T::LFE},  // film
        {T::left, T::right, T::centre, T::LFE, T::leftSurround, T::rightSurround},  // VST3 / SMPTE
        {T::left, T::right, T::leftSurround, T::rightSurround, T::centre, T::LFE},  // Logic
        {T::centre, T::left, T::right, T::leftSurround, T::rightSurround, T::LFE}}; // DTS
    const std::map<T, double> tone{{T::left, 41.0}, {T::right, 47.0}, {T::centre, 53.0},
                                   {T::leftSurround, 59.0}, {T::rightSurround, 67.0}, {T::LFE, 71.0}};
    SourceParams sp;
    sp.feedGain = {1.0, 0.5, 0.25, 0.0, 0.125};
    sp.mode = SourceMode::PerChannel;
    RouterParams rp;
    rp.mode = OutMode::Surround;
    rp.classGain = {0.9, 0.8, 0.7, 0.6, 0.5};
    rp.width = 1.0;
    std::vector<std::vector<double>> firstStreams;
    std::map<T, std::vector<double>> firstOut;
    for (size_t o = 0; o < orders.size(); ++o)
    {
        // buffer position of JUCE channel c
        std::vector<int> position(static_cast<size_t>(set.size()));
        for (int c = 0; c < set.size(); ++c)
            position[static_cast<size_t>(c)] = static_cast<int>(
                std::find(orders[o].begin(), orders[o].end(), set.getTypeOfChannel(c)) - orders[o].begin());
        const auto map = buildChannelMap(set, position, set.size());
        Channels in(orders[o].size());
        for (size_t b = 0; b < orders[o].size(); ++b)
            in[b] = sig::sine(tone.at(orders[o][b]), kFs, 0.3, -12.0);
        const auto streams = mix(map, in, sp);
        const auto out = route(map, rp, streams.left, streams.right);
        INFO("order " << o);
        if (o == 0)
        {
            firstStreams = {streams.left, streams.right};
            for (size_t b = 0; b < orders[o].size(); ++b)
                firstOut[orders[o][b]] = out[b];
            continue;
        }
        CHECK(streams.left == firstStreams[0]);
        CHECK(streams.right == firstStreams[1]);
        for (size_t b = 0; b < orders[o].size(); ++b)
            CHECK(out[b] == firstOut[orders[o][b]]);
    }
}

TEST_CASE("the routing stages do not allocate", "[routing]")
{
    const auto set = Set::create7point1point4();
    const auto map = mapOf(set);
    const auto x = sig::bass808(kFs);
    Channels in(static_cast<size_t>(set.size()), x);
    SourceMixer mixer;
    mixer.prepare(kFs, kBlock, map);
    SourceParams sp;
    sp.mode = SourceMode::Auto;
    mixer.setParams(sp);
    Router router;
    router.prepare(kFs, kBlock, map);
    RouterParams rp;
    rp.mode = OutMode::Surround;
    rp.classDelayMs = {1.0, 2.0, 3.0, 4.0, 5.0};
    router.setParams(rp);
    Summation sum;
    sum.prepare(kFs);
    sum.setParams(SumParams{});
    std::vector<double> sL(kBlock), sR(kBlock);
    Channels u(static_cast<size_t>(set.size()), std::vector<double>(kBlock)), out = u;
    std::vector<const double*> inPtr(in.size()), dryPtr(in.size()), subPtr(u.size());
    std::vector<double*> uPtr(u.size()), outPtr(out.size());
    size_t allocations = 0;
    for (size_t i = 0; i + kBlock <= x.size(); i += kBlock)
    {
        for (size_t c = 0; c < in.size(); ++c)
        {
            inPtr[c] = in[c].data() + i;
            dryPtr[c] = in[c].data() + i;
            uPtr[c] = u[c].data();
            subPtr[c] = u[c].data();
            outPtr[c] = out[c].data();
        }
        test::AllocGuard guard;
        if (i == 9600)
        {
            rp.mode = OutMode::Mono;
            router.setParams(rp);
            sp.mode = SourceMode::Max;
            mixer.setParams(sp);
        }
        mixer.process(inPtr.data(), kBlock, nullptr, 0, sL, sR);
        router.process(sL, sR, uPtr.data(), kBlock);
        sum.process(dryPtr.data(), subPtr.data(), outPtr.data(), set.size(), kBlock);
        allocations += guard.count();
    }
    CHECK(allocations == 0);
}

TEST_CASE("routing survives bad buffer indices, short streams, odd sample rates and big blocks", "[routing]")
{
    // a buffer index outside the block buffer is dropped, not followed
    const std::vector<int> wild{0, 7};
    const auto map = buildChannelMap(Set::stereo(), wild, 2);
    CHECK(map.ch[0].bufferIndex == 0);
    CHECK(map.ch[1].bufferIndex == -1);
    CHECK(map.ch[1].cls == ChannelClass::Other);
    std::vector<double> l(64, 0.5), r(64, 0.25), sL(64), sR(64);
    std::vector<const double*> in{l.data(), r.data()};
    SourceMixer mixer;
    mixer.prepare(kFs, 64, map);
    SourceParams louder;
    louder.mode = SourceMode::Max;
    mixer.setParams(louder);
    mixer.process(in.data(), 64, nullptr, 0, sL, sR);
    CHECK(sL.back() == 0.5); // the left channel alone: the right group lost its only channel
    Router router;
    router.prepare(kFs, 64, map);
    RouterParams rp;
    rp.mode = OutMode::Stereo;
    router.setParams(rp);
    std::vector<double> o0(64, 1.0), o1(64, 1.0);
    std::vector<double*> out{o0.data(), o1.data()};
    router.process(sL, {}, out.data(), 64);
    CHECK(o1.back() == 1.0); // channel 1 is nobody's: untouched
    // a layout over 32 channels maps nothing (isSupportedLayout rejects it)
    std::vector<int> many(40);
    std::iota(many.begin(), many.end(), 0);
    CHECK(buildChannelMap(Set::discreteChannels(40), many, 40).count == 0);
    // a stream shorter than the block: the rest of the output is silence, not stale samples
    const auto stereo = mapOf(Set::stereo());
    Router shortRouter;
    shortRouter.prepare(kFs, 64, stereo);
    shortRouter.setParams(rp);
    std::fill(o0.begin(), o0.end(), std::numeric_limits<double>::quiet_NaN());
    std::fill(o1.begin(), o1.end(), std::numeric_limits<double>::quiet_NaN());
    shortRouter.process(std::span<const double>(sL).first(32), {}, out.data(), 64);
    CHECK(std::all_of(o0.begin() + 32, o0.end(), [](double v) { return v == 0.0; }));
    CHECK(std::all_of(o1.begin() + 32, o1.end(), [](double v) { return v == 0.0; }));
    // sample rates outside 8 kHz ... 768 kHz are clamped (a 10 ms route delay at "10 MHz" is 7680 samples)
    CHECK(sanitizeSampleRate(1e12) == 768000.0);
    CHECK(sanitizeSampleRate(1.0) == 8000.0);
    CHECK(sanitizeSampleRate(std::numeric_limits<double>::quiet_NaN()) == 48000.0);
    CHECK(sanitizeSampleRate(-44100.0) == 48000.0);
    CHECK(sanitizeSampleRate(96000.0) == 96000.0);
    Router fast;
    fast.prepare(1e7, 64, stereo);
    RouterParams delayed = rp;
    delayed.classDelayMs[0] = 10.0;
    fast.setParams(delayed);
    std::vector<double> pulse(8192, 0.0), f0(8192), f1(8192);
    pulse[0] = 1.0;
    std::vector<double*> fo{f0.data(), f1.data()};
    for (size_t i = 0; i < pulse.size(); i += 64)
    {
        std::vector<double*> at{f0.data() + i, f1.data() + i};
        fast.process(std::span<const double>(pulse).subspan(i, 64), {}, at.data(), 64);
    }
    CHECK(f0[7680] == 1.0);
    // a block longer than maxBlock is processed whole, in chunks: Detect gives the key on every sample
    const auto mono = mapOf(Set::mono());
    SourceMixer keyed;
    keyed.prepare(kFs, 64, mono);
    SourceParams kp;
    kp.keyMode = KeyMode::Detect;
    keyed.setParams(kp);
    std::vector<double> src(4096, 0.0), key(4096, 0.125), kL(4096), kR(4096);
    const double* srcPtr = src.data();
    const double* keyPtr = key.data();
    keyed.process(&srcPtr, 4096, &keyPtr, 1, kL, kR);
    CHECK(std::all_of(kL.begin(), kL.end(), [](double v) { return v == 0.125; }));
}
