// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

#include "chain/PhaseTool.h"
#include "core/SampleRate.h"
#include "params/Sanitise.h"
#include "plugin/ParamMapping.h"

namespace subgen::plugin
{
using params::ParamId;

namespace
{
constexpr double kMuteMs = 20.0;
constexpr double kBypassMs = 10.0;
constexpr double kSilenceResetSeconds = 5.0;
constexpr double kMaxAlignMs = 40.0;
constexpr double kMeterMs = 300.0;

double sCurve(double u) { return 0.5 - 0.5 * std::cos(std::numbers::pi * u); }

// the layouts the processor can work with (SPEC_IO_UI §2.1-2.2): a supported main pair, a mono or stereo sidechain
bool layoutUsable(const juce::AudioProcessor::BusesLayout& layouts)
{
    if (layouts.inputBuses.isEmpty() || layouts.outputBuses.isEmpty())
        return false;
    if (!routing::isSupportedPair(layouts.getMainInputChannelSet(), layouts.getMainOutputChannelSet()))
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto& key = layouts.inputBuses.getReference(1);
        if (!key.isDisabled() && key != juce::AudioChannelSet::mono() && key != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

// out_mode as the router resolves it (SPEC_IO_UI §2.2, §3)
routing::OutMode resolvedOutMode(const params::ParamSnapshot& s, bool hasLfe)
{
    const auto mode = static_cast<routing::OutMode>(std::clamp(s.index(ParamId::out_mode), 0, 4));
    if (mode == routing::OutMode::Auto)
        return hasLfe ? routing::OutMode::Lfe : routing::OutMode::Mono;
    return mode == routing::OutMode::Lfe && !hasLfe ? routing::OutMode::Mono : mode;
}
} // namespace

SubGenProcessor::SubGenProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "SUBGEN", params::createParameterLayout()),
      view(state)
{
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        startTimerHz(20); // the message thread's half of a configuration change
}

SubGenProcessor::~SubGenProcessor() { stopTimer(); }

bool SubGenProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const { return layoutUsable(layouts); }

void SubGenProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    fs = sanitizeSampleRate(sampleRate);
    hostBlock = std::max(1, maximumExpectedSamplesPerBlock);
    warningBits.set(Warning::LatencyChanged, false); // playback (re)starts with the latency reported here
    configure(false);
    setLatencySamples(latencyNow.load());
}

void SubGenProcessor::releaseResources() {}

void SubGenProcessor::configure(bool fadeIn)
{
    passThrough = !layoutUsable(getBusesLayout());
    warningBits.set(Warning::LayoutNotSupported, passThrough);
    inChannels = std::min(getMainBusNumInputChannels(), kMaxChannels);
    const auto* key = getBus(true, 1);
    keyChannels = key != nullptr && key->isEnabled() ? std::min(key->getNumberOfChannels(), 2) : 0;
    outChannels = std::min(getMainBusNumOutputChannels(), kMaxChannels);
    reconfigure.store(false);
    if (passThrough)
    {
        appliedLatency = 0;
        latencyNow.store(0);
        osLatency.store(0);
        return;
    }
    view.read(raw);
    snapshot = raw;
    params::sanitise(snapshot);
    quality = qualityOf(snapshot);
    chunk = std::max(hostBlock, kSubBlock);

    const auto inSet = getChannelLayoutOfBus(true, 0);
    const auto outSet = getChannelLayoutOfBus(false, 0);
    std::vector<int> order(static_cast<size_t>(std::max(inChannels, outChannels)));
    std::iota(order.begin(), order.end(), 0);
    inMap = routing::buildChannelMap(inSet, order, inChannels);
    outMap = routing::buildChannelMap(outSet, order, outChannels);
    for (int j = 0; j < outChannels; ++j) // the dry of an output channel: the input channel of its type (mono's is centre)
    {
        const int from = inSet.getChannelIndexForType(outSet.getTypeOfChannel(j));
        dryFrom[static_cast<size_t>(j)] = from < inChannels ? from : -1;
    }

    mixer.prepare(fs, kSubBlock, inMap);
    for (auto& h : hosts)
        h.prepare(fs, kSubBlock, quality);
    for (auto& c : chains)
    {
        c.prepare(fs, kSubBlock, quality);
        c.setNonRealtime(nonRealtime);
    }
    router.prepare(fs, kSubBlock, outMap);
    summation.prepare(fs);
    osLatency.store(chains[0].oversamplingLatencySamples());
    const int capacity = computeLatency(quality, true, kMaxAlignMs, fs, osLatency.load()).total + 1;
    dryDelay.resize(static_cast<size_t>(outChannels));
    for (auto& d : dryDelay)
        d.prepare(capacity);
    inBuffer.setSize(std::max(1, inChannels + keyChannels), chunk);
    dryBuffer.setSize(std::max(1, outChannels), chunk);
    subBuffer.setSize(std::max(1, outChannels), chunk);
    outBuffer.setSize(std::max(1, outChannels), chunk);
    for (auto* v : {&streamL, &streamR, &sub0, &sub1, &keyScratch})
        v->assign(kSubBlock, 0.0);
    subCorrelation.prepare(fs);
    frontBand.prepare(fs);
    frontBand.setRange(20.0, 120.0, 24);
    frontRms.prepare(fs, kMeterMs);
    lfeRms.prepare(fs, kMeterMs);
    sourceRms.prepare(fs, kMeterMs);
    sampleCounter = 0;
    silentFor = 0;
    silentReset = false;
    madeFSub = {-1.0, -1.0};

    paramsChanged = true;
    applyParameters(); // the first parameters after prepare: every stage starts without ramps
    appliedLatency = desiredLatency();
    for (auto& d : dryDelay)
        d.setDelay(appliedLatency);
    latencyNow.store(appliedLatency);
    muting = false;
    mute.reset(fs, kMuteMs, fadeIn ? 0.0 : 1.0);
    if (fadeIn)
        mute.setTarget(1.0);
    bypassFade.reset(fs, kBypassMs, snapshot.on(ParamId::bypass) ? 1.0 : 0.0);
}

void SubGenProcessor::reset()
{
    resetStates();
    for (auto& d : dryDelay)
        d.reset();
}

void SubGenProcessor::resetStates()
{
    mixer.reset();
    for (auto& h : hosts)
        h.reset();
    for (auto& c : chains)
        c.reset();
    router.reset();
    subCorrelation.reset();
    madeFSub = {-1.0, -1.0};
    paramsChanged = true; // the reset stages take their parameters afresh
}

void SubGenProcessor::readParameters()
{
    view.read(raw);
    snapshot = raw;
    params::sanitise(snapshot);
    paramsChanged = paramsChanged || snapshot.raw != applied.raw;
    // W-08 sees the parameters as set; W-10 is sanitise()'s own correction (range_lo moved under range_hi / 1.2)
    warningBits.set(Warning::LinearPhaseNeedsPrint, raw.index(ParamId::filt_phase) == 1 && raw.index(ParamId::quality) != 2);
    warningBits.set(Warning::RangeCorrected, snapshot[ParamId::range_lo] != raw[ParamId::range_lo]);
}

void SubGenProcessor::applyParameters()
{
    const bool keyAvailable = keyChannels > 0;
    if (paramsChanged)
    {
        const auto source = sourceParamsOf(snapshot, keyAvailable);
        mixer.setParams(source);
        router.setParams(routerParamsOf(snapshot));
        summation.setParams(sumParamsOf(snapshot));
        const auto control = engineControlOf(snapshot, source.keyMode == routing::KeyMode::Trigger);
        for (auto& h : hosts)
        {
            h.setEngine(engineOf(snapshot));
            h.setControl(control);
        }
        bypassFade.setTarget(snapshot.on(ParamId::bypass) ? 1.0 : 0.0);
        warningBits.set(Warning::NoLfeChannel, snapshot.index(ParamId::out_mode) == 4 && !outMap.hasLfe);
        warningBits.set(Warning::KeyNotConnected, snapshot.index(ParamId::sc_use) != 0 && !keyAvailable);
    }
    const bool lfeMode = resolvedOutMode(snapshot, outMap.hasLfe) == routing::OutMode::Lfe;
    const bool hasPitch = engineOf(snapshot) != engines::EngineType::Harmonic;
    for (size_t k = 0; k < chains.size(); ++k)
    {
        const double reported = hosts[k].subFrequencyHz();
        const double f = std::isfinite(reported) && reported > 0.0 ? reported : static_cast<double>(snapshot[ParamId::base_freq]);
        if (paramsChanged || std::abs(f - madeFSub[k]) > 1e-9 * f)
        {
            chains[k].setParams(subChainParamsOf(snapshot, f, hasPitch, lfeMode));
            madeFSub[k] = f;
        }
    }
    applied = snapshot;
    paramsChanged = false;
}

int SubGenProcessor::alignSamples() const
{
    return msToSamples(std::clamp(static_cast<double>(snapshot[ParamId::align_ms]), 0.0, kMaxAlignMs), fs);
}

int SubGenProcessor::desiredLatency() const
{
    return hosts[0].latencySamples() + chains[0].latencySamples() + alignSamples();
}

void SubGenProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process(buffer); }

void SubGenProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { process(buffer); }

void SubGenProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { bypassed(buffer); }

void SubGenProcessor::processBlockBypassed(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { bypassed(buffer); }

template <typename Sample>
void SubGenProcessor::process(juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    if (passThrough) // W-12: the buffer already holds the input
        return;
    readParameters();
    const int n = buffer.getNumSamples();
    const int available = buffer.getNumChannels();
    blockPeak = 0.0;
    for (int done = 0; done < n;)
    {
        const int len = std::min(n - done, chunk);
        for (int c = 0; c < inChannels + keyChannels; ++c)
        {
            auto* dst = inBuffer.getWritePointer(c);
            if (c < available)
            {
                const auto* src = buffer.getReadPointer(c, done);
                for (int i = 0; i < len; ++i)
                    dst[i] = static_cast<double>(src[i]);
            }
            else
                std::fill(dst, dst + len, 0.0);
        }
        for (int pos = 0; pos < len;)
        {
            const int sub = std::min(len - pos, kSubBlock - static_cast<int>(sampleCounter % kSubBlock));
            processSubBlock(pos, sub);
            pos += sub;
            sampleCounter += sub;
        }
        for (int j = 0; j < outChannels && j < available; ++j)
        {
            const auto* src = outBuffer.getReadPointer(j);
            auto* dst = buffer.getWritePointer(j, done);
            for (int i = 0; i < len; ++i)
                dst[i] = static_cast<Sample>(src[i]);
        }
        done += len;
    }
    for (int c = outChannels; c < available; ++c) // the sidechain's channels and anything past the main output
        buffer.clear(c, 0, n);
    warningBits.set(Warning::Clip, blockPeak >= 1.0);
}

template <typename Sample>
void SubGenProcessor::bypassed(juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    if (passThrough)
        return;
    const int n = buffer.getNumSamples();
    const int available = buffer.getNumChannels();
    for (int done = 0; done < n;)
    {
        const int len = std::min(n - done, chunk);
        for (int c = 0; c < inChannels && c < available; ++c)
        {
            const auto* src = buffer.getReadPointer(c, done);
            auto* dst = inBuffer.getWritePointer(c);
            for (int i = 0; i < len; ++i)
                dst[i] = static_cast<double>(src[i]);
        }
        for (int j = 0; j < outChannels && j < available; ++j) // the dry delayed by L_total: the latency stays
        {
            const int from = dryFrom[static_cast<size_t>(j)];
            auto* dst = buffer.getWritePointer(j, done);
            for (int i = 0; i < len; ++i)
                dst[i] = static_cast<Sample>(dryDelay[static_cast<size_t>(j)].process(from >= 0 ? inBuffer.getSample(from, i) : 0.0));
        }
        done += len;
    }
}

void SubGenProcessor::processSubBlock(int offset, int n)
{
    const auto len = static_cast<size_t>(n);
    applyParameters();

    // digital silence for 5 s: fresh states (denormal tails, stale estimates)
    bool silent = true;
    for (int c = 0; c < inChannels + keyChannels && silent; ++c)
    {
        const auto* x = inBuffer.getReadPointer(c, offset);
        for (int i = 0; i < n && silent; ++i)
            silent = x[i] == 0.0;
    }
    silentFor = silent ? silentFor + n : 0;
    if (!silent)
        silentReset = false;
    else if (!silentReset && static_cast<double>(silentFor) >= kSilenceResetSeconds * fs)
    {
        resetStates();
        applyParameters();
        silentReset = true;
    }

    // S1, S2: the source streams; a non-finite source sample counts as 0 (SPEC §4.7)
    std::array<const double*, kMaxChannels> in{};
    for (int c = 0; c < inChannels; ++c)
        in[static_cast<size_t>(c)] = inBuffer.getReadPointer(c, offset);
    std::array<const double*, 2> keyIn{};
    for (int c = 0; c < keyChannels; ++c)
        keyIn[static_cast<size_t>(c)] = inBuffer.getReadPointer(inChannels + c, offset);
    const auto sl = std::span<double>(streamL).first(len);
    const auto sr = std::span<double>(streamR).first(len);
    mixer.process(in.data(), n, keyChannels > 0 ? keyIn.data() : nullptr, keyChannels, sl, sr);
    for (size_t i = 0; i < len; ++i)
    {
        sl[i] = std::isfinite(sl[i]) ? sl[i] : 0.0;
        sr[i] = std::isfinite(sr[i]) ? sr[i] : 0.0;
    }
    const auto keyStream = mixer.keyStream();
    const size_t keyLength = std::min(keyStream.size(), len);
    for (size_t i = 0; i < keyLength; ++i)
        keyScratch[i] = std::isfinite(keyStream[i]) ? keyStream[i] : 0.0;
    const auto key = std::span<const double>(keyScratch).first(keyLength);

    // S5, S6 per stream; a sub chain that caught a NaN or Inf resets its engines too (SPEC §4.7)
    const int streams = mixer.numStreams();
    const auto s0 = std::span<double>(sub0).first(len);
    const auto s1 = std::span<double>(sub1).first(len);
    hosts[0].process(sl, key, s0);
    chains[0].process(s0);
    if (streams == 2)
    {
        hosts[1].process(sr, key, s1);
        chains[1].process(s1);
    }
    for (size_t k = 0; k < chains.size(); ++k)
        if (chains[k].nanEvent())
            hosts[k].reset();
    const double reported = hosts[0].subFrequencyHz();
    if (std::isfinite(reported) && reported > 0.0)
        fSub1.store(reported, std::memory_order_relaxed);

    // S7
    std::array<double*, kMaxChannels> u{};
    for (int j = 0; j < outChannels; ++j)
        u[static_cast<size_t>(j)] = subBuffer.getWritePointer(j, offset);
    router.process(s0, streams == 2 ? std::span<const double>(s1) : std::span<const double>{}, u.data(), n);

    // the dry, delayed by L_total
    for (int j = 0; j < outChannels; ++j)
    {
        const int from = dryFrom[static_cast<size_t>(j)];
        auto* d = dryBuffer.getWritePointer(j, offset);
        const double* x = from >= 0 ? inBuffer.getReadPointer(from, offset) : nullptr;
        auto& line = dryDelay[static_cast<size_t>(j)];
        for (int i = 0; i < n; ++i)
            d[i] = line.process(x != nullptr ? x[i] : 0.0);
    }

    // S8, then the bypass and the mute
    std::array<const double*, kMaxChannels> dryPtr{}, subPtr{};
    std::array<double*, kMaxChannels> outPtr{};
    for (int j = 0; j < outChannels; ++j)
    {
        dryPtr[static_cast<size_t>(j)] = dryBuffer.getReadPointer(j, offset);
        subPtr[static_cast<size_t>(j)] = subBuffer.getReadPointer(j, offset);
        outPtr[static_cast<size_t>(j)] = outBuffer.getWritePointer(j, offset);
    }
    summation.process(dryPtr.data(), subPtr.data(), outPtr.data(), outChannels, n);
    for (int i = 0; i < n; ++i)
    {
        const double b = sCurve(bypassFade.next());
        const double m = sCurve(mute.next());
        for (int j = 0; j < outChannels; ++j)
        {
            double y = outPtr[static_cast<size_t>(j)][i];
            if (b != 0.0)
                y = b == 1.0 ? dryPtr[static_cast<size_t>(j)][i] : (1.0 - b) * y + b * dryPtr[static_cast<size_t>(j)][i];
            if (m != 1.0)
                y *= m;
            outPtr[static_cast<size_t>(j)][i] = y;
            blockPeak = std::max(blockPeak, std::abs(y));
        }
    }
    updateMeters(offset, n, streams);

    // a new latency (quality, filt_phase, align_ms): mute, switch at silence, come back; quality needs the message thread
    const int desired = desiredLatency();
    const bool qualityPending = qualityOf(snapshot) != quality;
    if ((desired != appliedLatency || qualityPending) && !muting)
    {
        muting = true;
        mute.setTarget(0.0);
    }
    if (muting && !mute.isSmoothing() && mute.current() == 0.0)
    {
        if (qualityPending)
            reconfigure.store(true);
        else
        {
            for (auto& d : dryDelay)
                d.setDelay(desired);
            appliedLatency = desired;
            latencyNow.store(desired);
            muting = false;
            mute.setTarget(1.0);
        }
    }
}

void SubGenProcessor::updateMeters(int offset, int n, int streams)
{
    // W-03: the source out of phase in Sum
    warningBits.set(Warning::SourceOutOfPhase, snapshot.index(ParamId::src_mode) == 0 && mixer.correlation() < -0.5);
    // W-04: two sub streams cancelling in mono
    if (streams == 2)
        for (int i = 0; i < n; ++i)
            subCorrelation.process(sub0[static_cast<size_t>(i)], sub1[static_cast<size_t>(i)]);
    warningBits.set(Warning::SubCancelsInMono, streams == 2 && subCorrelation.current() < -0.5);
    // W-05: LFE mode without bleed, the LFE sub over the Front channels' 20-120 Hz by more than -6 dB
    const bool lfeMode = resolvedOutMode(snapshot, outMap.hasLfe) == routing::OutMode::Lfe;
    std::array<int, kMaxChannels> frontInputs{}; // the input channels feeding the Front outputs' dry
    int frontCount = 0, lfeChannel = -1;
    for (int j = 0; j < outChannels; ++j) // outMap holds output channel j at buffer index j
    {
        const auto cls = outMap.ch[static_cast<size_t>(j)].cls;
        const int from = dryFrom[static_cast<size_t>(j)];
        if (cls == routing::ChannelClass::Front && from >= 0)
            frontInputs[static_cast<size_t>(frontCount++)] = from;
        if (cls == routing::ChannelClass::Lfe && lfeChannel < 0)
            lfeChannel = j;
    }
    for (int i = 0; i < n; ++i)
    {
        double front = 0.0;
        for (int k = 0; k < frontCount; ++k)
            front += inBuffer.getSample(frontInputs[static_cast<size_t>(k)], offset + i);
        frontRms.process(frontBand.process(frontCount > 0 ? front / frontCount : 0.0));
        lfeRms.process(lfeChannel >= 0 ? subBuffer.getSample(lfeChannel, offset + i) : 0.0);
        sourceRms.process(streamL[static_cast<size_t>(i)]);
    }
    const double frontLevel = frontRms.current(), lfeLevel = lfeRms.current();
    warningBits.set(Warning::LfeOnlyContent, lfeMode && snapshot.gain(ParamId::lfe_bleed) == 0.0 && lfeLevel > 1e-5
                                                 && lfeLevel > frontLevel * std::pow(10.0, -6.0 / 20.0));
    // W-09: a confidence gate closed while the source plays
    const bool playing = 20.0 * std::log10(std::max(sourceRms.current(), 1e-12)) > snapshot[ParamId::det_thresh];
    bool gated = !hosts[0].gateOpen() || (streams == 2 && !hosts[1].gateOpen());
    warningBits.set(Warning::LowConfidence, playing && gated);
}

void SubGenProcessor::setNonRealtime(bool isNonRealtime) noexcept
{
    AudioProcessor::setNonRealtime(isNonRealtime);
    nonRealtime = isNonRealtime;
    for (auto& c : chains)
        c.setNonRealtime(isNonRealtime);
}

juce::AudioProcessorParameter* SubGenProcessor::getBypassParameter() const { return state.getParameter("bypass"); }

LatencyBreakdown SubGenProcessor::latencyBreakdown() const
{
    params::ParamSnapshot s;
    view.read(s);
    params::sanitise(s);
    if (passThrough)
        return {};
    return computeLatency(quality, quality == Quality::Print && linearPhaseOf(s), s[ParamId::align_ms], fs, osLatency.load());
}

double SubGenProcessor::estimatedSubDelayMs(double fSub1Hz) const
{
    params::ParamSnapshot s;
    view.read(s);
    params::sanitise(s);
    const double f = std::isfinite(fSub1Hz) && fSub1Hz > 0.0 ? fSub1Hz : static_cast<double>(s[ParamId::base_freq]);
    const auto type = engineOf(s);
    double ms = 0.0;
    if (type != engines::EngineType::Trigger) // TRIGGER's sub is synthetic: no source band in its path
    {
        const auto control = engineControlOf(s, false);
        engines::SourceBand band;
        band.prepare(fs);
        const bool harmonic = type == engines::EngineType::Harmonic;
        const bool fixed = control.baseFixed && type != engines::EngineType::Track && type != engines::EngineType::Hybrid;
        if (fixed)
            band.setBandpass(harmonic ? control.baseFreqHz : 2.0 * control.baseFreqHz);
        else
            band.setRange(control.rangeLoHz, control.rangeHiHz, control.rangeSlope);
        ms += band.groupDelayMs(harmonic ? f : 2.0 * f); // the source's frequency
    }
    const bool lfeMode = resolvedOutMode(s, outMap.hasLfe) == routing::OutMode::Lfe;
    const auto filters = subChainParamsOf(s, f, type != engines::EngineType::Harmonic, lfeMode).filters;
    ms += chain::subFiltersGroupDelayMs(filters, quality == Quality::Print && filters.linearPhase, fs, f);
    chain::HilbertPair pair;
    pair.prepare(fs);
    return ms + pair.groupDelayMsA(f);
}

void SubGenProcessor::applyAutoAlignMs()
{
    const double ms = std::clamp(std::round(estimatedSubDelayMs() * 10.0) / 10.0, 0.0, kMaxAlignMs);
    if (auto* p = state.getParameter("align_ms"))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(ms)));
}

void SubGenProcessor::applyPendingConfiguration()
{
    if (reconfigure.exchange(false))
    {
        suspendProcessing(true); // the audio thread is out of processBlock while the stages are re-prepared
        configure(true);
        suspendProcessing(false);
    }
    const int now = latencyNow.load();
    if (now != getLatencySamples())
    {
        setLatencySamples(now);
        warningBits.set(Warning::LatencyChanged, true);
    }
}

bool SubGenProcessor::waitForLinearPhase(int timeoutMs) const
{
    return chains[0].waitForLinearPhase(timeoutMs) && chains[1].waitForLinearPhase(timeoutMs);
}

juce::AudioProcessorEditor* SubGenProcessor::createEditor() { return new juce::GenericAudioProcessorEditor(*this); }

const juce::String SubGenProcessor::getName() const { return JucePlugin_Name; }

void SubGenProcessor::getStateInformation(juce::MemoryBlock&) {}

void SubGenProcessor::setStateInformation(const void*, int) {}
} // namespace subgen::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new subgen::plugin::SubGenProcessor(); }
