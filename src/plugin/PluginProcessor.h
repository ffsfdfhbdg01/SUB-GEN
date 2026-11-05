// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "analysis/AutoAlign.h"
#include "analysis/CaptureBuffer.h"
#include "chain/SubChain.h"
#include "chain/Weight.h"
#include "core/Quality.h"
#include "dsp/DelayLine.h"
#include "dsp/Envelope.h"
#include "dsp/Smoother.h"
#include "engines/EngineHost.h"
#include "engines/SourceBand.h"
#include "params/Parameters.h"
#include "plugin/Latency.h"
#include "plugin/ParamUndo.h"
#include "plugin/StateIO.h"
#include "plugin/UiFeed.h"
#include "plugin/Warnings.h"
#include "routing/ChannelMap.h"
#include "routing/Correlation.h"
#include "routing/Router.h"
#include "routing/SourceMixer.h"
#include "routing/Summation.h"

namespace subgen::plugin
{
// The SUB-GEN processor (SPEC §4): main in/out (stereo by default) and an optional mono or stereo sidechain. Each block:
// the parameters are read once (atomic) and sanitised, the input copied to double, then sub-blocks of at most 64
// samples on an absolute sample counter (so the host's block size never changes the result) run S1 source -> S2
// detection -> engines -> sub chain per stream -> S7 router -> S8 summation, the dry through a delay of L_total. The
// bypass parameter crossfades over 10 ms to the delayed dry. A change of quality, filt_phase or align_ms mutes the output
// over 20 ms; align_ms and filt_phase then switch on the audio thread, quality is re-prepared by the message thread
// (timer) with processing suspended; the output comes back over 20 ms and the message thread reports the latency
// (W-07). States reset after 5 s of digital silence. No allocation, lock or wait in processBlock. The host's state is
// versioned, compressed XML (StateIO); Auto Align analyses the last seconds of S1 and the sub on a background thread.
class SubGenProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SubGenProcessor();
    ~SubGenProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    void setNonRealtime(bool isNonRealtime) noexcept override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // the parameter registry (PARAMETERS.md)
    juce::AudioProcessorValueTreeState& parameters() { return state; }
    const juce::AudioProcessorValueTreeState& parameters() const { return state; }

    const Warnings& warnings() const { return warningBits; }
    LatencyBreakdown latencyBreakdown() const; // of the configuration prepared and applied
    double subFrequencyHz() const { return fSub1.load(std::memory_order_relaxed); } // stream 1's Sub -1
    // SPEC §9.2: the sub path's natural group delay at f_sub1 (the source band at the source's frequency, the sub
    // filters and chain A), from the current parameters; message thread
    double estimatedSubDelayMs(double fSub1Hz) const;
    double estimatedSubDelayMs() const { return estimatedSubDelayMs(subFrequencyHz()); }
    void applyAutoAlignMs(); // the Auto action: align_ms = the estimate, 0.1 ms steps, 0-40 ms (T-ALN-01)
    // Message thread (the timer; tests call it): re-prepares after a quality change, reports a new latency.
    void applyPendingConfiguration();
    // Off the audio thread (tests, offline rendering): waits for the Linear Phase kernels asked for.
    bool waitForLinearPhase(int timeoutMs) const;

    // The state outside the parameters (PARAMETERS §6): UI, the last Auto Align result, the preset. Any thread.
    ExtraState extraState() const;
    void setExtraState(const ExtraState& newExtra);
    // Auto Align (SPEC §8.B; message thread): analyses the capture on a background thread. The message thread then
    // (timer, applyPendingConfiguration) writes the result to <ALIGN/> and, with a clear optimum and align_lock off,
    // to sub_pol and sub_rot. A request while one runs is ignored.
    void requestAutoAlign();
    bool waitForAutoAlign(int timeoutMs) const; // off the audio thread: the analysis asked for has finished
    analysis::AlignResult lastAutoAlign() const;
    int autoAlignCount() const { return alignResults; } // results applied so far (message thread)

    // The editor's view of the audio (written only while an editor is open) and its undo history.
    UiFeed& uiFeed() { return feed; }
    ParamUndo& paramUndo() { return undoHistory; }

private:
    static constexpr int kSubBlock = 64;
    static constexpr int kMaxChannels = 32;

    // one Auto Align analysis, shared with the background thread, which owns no part of the processor
    struct AlignJob
    {
        juce::WaitableEvent finished{true};
        std::atomic<bool> done{false};
        analysis::AlignResult result;
    };

    void timerCallback() override { applyPendingConfiguration(); }
    void applyAutoAlignResult(const analysis::AlignResult& r); // message thread
    void configure(bool fadeIn); // prepares every stage for the current parameters (allocates)
    template <typename Sample>
    void process(juce::AudioBuffer<Sample>& buffer);
    template <typename Sample>
    void bypassed(juce::AudioBuffer<Sample>& buffer);
    void readParameters();
    void applyParameters();
    void processSubBlock(int offset, int n);
    void updateMeters(int offset, int n, int streams);
    void resetStates();
    int alignSamples() const;
    int desiredLatency() const;

    juce::AudioProcessorValueTreeState state;
    ParamUndo undoHistory{state};
    params::ParamView view;
    params::ParamSnapshot raw;      // as read
    params::ParamSnapshot snapshot; // sanitised
    params::ParamSnapshot applied;  // what the stages were last given
    bool paramsChanged = true;
    Warnings warningBits;

    // the configuration (configure() on prepareToPlay or the message thread)
    double fs = 48000.0;
    int hostBlock = 512;
    int chunk = 512;
    Quality quality = Quality::Mix;
    bool passThrough = false; // W-12
    int inChannels = 0;
    int keyChannels = 0;
    int outChannels = 0;
    routing::ChannelMap inMap;
    routing::ChannelMap outMap;
    std::array<int, kMaxChannels> dryFrom{}; // output channel -> input channel, -1: none
    bool nonRealtime = false;

    routing::SourceMixer mixer;
    std::array<engines::EngineHost, 2> hosts;
    std::array<chain::SubChain, 2> chains;
    std::array<double, 2> madeFSub{-1.0, -1.0}; // the f_sub1 the chains were given
    routing::Router router;
    routing::Summation summation;
    std::vector<dsp::DelayLine> dryDelay;
    std::array<chain::Weight, kMaxChannels> weights; // per output channel, on the dry into the summation
    bool weightFresh = true;

    juce::AudioBuffer<double> inBuffer;  // main input, then the sidechain
    juce::AudioBuffer<double> dryBuffer; // the delayed dry per output channel (what bypass gives)
    juce::AudioBuffer<double> weightedBuffer; // the dry through Weight (what the summation takes)
    juce::AudioBuffer<double> subBuffer; // u per output channel
    juce::AudioBuffer<double> outBuffer;
    std::vector<double> streamL, streamR, sub0, sub1, keyScratch;

    // latency switching
    int appliedLatency = 0;
    std::atomic<int> latencyNow{0};
    std::atomic<bool> reconfigure{false};
    std::atomic<int> osLatency{0};
    dsp::LinearSmoother mute; // 1: sounding, 0: muted
    bool muting = false;
    dsp::LinearSmoother bypassFade;
    double blockPeak = 0.0;
    std::int64_t sampleCounter = 0;
    std::int64_t silentFor = 0;
    bool silentReset = false;

    // meters for the warnings
    routing::Correlation subCorrelation;
    engines::SourceBand frontBand;
    dsp::RmsFollower frontRms;
    dsp::RmsFollower lfeRms;
    dsp::RmsFollower sourceRms;
    std::atomic<double> fSub1{0.0};

    // Auto Align: S1 delayed to meet chain A's sub where the two sum at the output, and that sub
    analysis::CaptureBuffer capture;
    dsp::DelayLine alignDelay;
    std::vector<double> alignDry, alignSub;
    std::unique_ptr<juce::ThreadPool> alignPool; // made by the first request
    std::shared_ptr<AlignJob> alignJob;          // the last request's (message thread)

    // the editor's feed: the Front outputs' mean and the outputs' peak per sample of a sub-block
    UiFeed feed;
    std::array<double, kSubBlock> uiSum{}, uiPeak{};

    mutable juce::CriticalSection extraLock;
    ExtraState extra;
    analysis::AlignResult alignResult;
    int alignResults = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SubGenProcessor)
};
} // namespace subgen::plugin
