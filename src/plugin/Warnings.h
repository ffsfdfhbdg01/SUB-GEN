// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

namespace subgen::plugin
{
// SPEC_IO_UI §7.4; a warning's value is its number.
enum class Warning : unsigned
{
    NoLfeChannel = 1,          // W-01 out_mode LFE without an LFE channel: Mono
    KeyNotConnected = 2,       // W-02 sc_use without a sidechain bus: the main input
    SourceOutOfPhase = 3,      // W-03 rho < -0.5 in Sum
    SubCancelsInMono = 4,      // W-04 the two sub streams cancel in mono (rho < -0.5)
    LfeOnlyContent = 5,        // W-05 LFE content lost in a stereo downmix
    Clip = 6,                  // W-06 output peak at or over 0 dBFS
    LatencyChanged = 7,        // W-07 quality, filt_phase or align_ms changed: restart playback
    LinearPhaseNeedsPrint = 8, // W-08 Linear Phase asked outside Print: Min Phase
    LowConfidence = 9,         // W-09 the confidence gate holds the sub back while the source plays
    RangeCorrected = 10,       // W-10 range_hi < 1.2 range_lo: sanitise() moved range_lo
    PreviewDisabled = 11,      // W-11 (UI, Phase 4)
    LayoutNotSupported = 12    // W-12 a layout forced on the plug-in: passed through
};

// The warnings as one atomic set of bits: the audio and the message thread write, the UI reads.
class Warnings
{
public:
    void set(Warning w, bool on)
    {
        const std::uint32_t bit = 1u << static_cast<unsigned>(w);
        if (on)
            bits.fetch_or(bit, std::memory_order_relaxed);
        else
            bits.fetch_and(~bit, std::memory_order_relaxed);
    }

    bool has(Warning w) const { return ((bits.load(std::memory_order_relaxed) >> static_cast<unsigned>(w)) & 1u) != 0; }
    std::uint32_t all() const { return bits.load(std::memory_order_relaxed); }
    void clear() { bits.store(0, std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> bits{0};
};
} // namespace subgen::plugin
