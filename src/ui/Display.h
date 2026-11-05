// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "chain/Eq.h"
#include "ui/Controls.h"
#include "ui/Spectrum.h"

namespace subgen::ui
{
// The analyser (design doc §3): the dry, sub and sum spectra over 10-500 Hz, the source band with its two handles
// (range_lo, range_hi) dragged on the graph, the Sub −1 pitch, and on the EQ page the EQ curve with a node per band
// (drag: frequency and gain, wheel: Q, double-click: band on or off).
class Display : public juce::Component
{
public:
    using Levels = std::array<float, Spectrum::kPoints>;

    Display(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo);

    void setLevels(const Levels& dry, const Levels& sub, const Levels& sum);
    void setPitch(double hz, bool sounding);
    void setSampleRate(double fs);
    void setEqVisible(bool visible);
    bool eqVisible() const { return showEq; }

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    struct EqBandBindings
    {
        std::unique_ptr<ParamBinding> on, type, freq, gain, q;
    };
    enum class Target
    {
        None,
        RangeLo,
        RangeHi,
        EqNode
    };

    juce::Rectangle<float> plot() const;
    float xFor(double hz) const;
    double hzFor(float x) const;
    float yForDb(float db) const;   // the spectrum's scale
    float yForEqDb(double db) const; // the EQ's scale (±18 dB)
    double eqDbFor(float y) const;
    juce::Point<float> nodePosition(int band) const;
    std::pair<Target, int> hit(juce::Point<float> p) const;
    void drawGrid(juce::Graphics& g, juce::Rectangle<float> area);
    void drawSpectrum(juce::Graphics& g, const Levels& levels, juce::Colour colour, bool fill, float width);
    void drawRange(juce::Graphics& g, juce::Rectangle<float> area);
    void drawEq(juce::Graphics& g, juce::Rectangle<float> area);
    void bindChanged() { repaint(); }

    std::unique_ptr<ParamBinding> rangeLo, rangeHi;
    std::array<EqBandBindings, 5> eq;
    chain::Eq eqCurve;
    double fs = 48000.0;
    Levels dry{}, sub{}, sum{};
    double pitchHz = 0.0;
    bool pitchSounding = false;
    bool showEq = false;
    Target dragging = Target::None;
    int dragBand = -1;
    Target hover = Target::None;
    int hoverBand = -1;
};

// The level meters beside the analyser: In (the source), Sub, Out (peak and 300 ms RMS, clip light), and the gain
// reduction of the compressor and the limiter while they are on.
class Meters : public juce::Component
{
public:
    struct Values
    {
        float inPeak = -120.0f, inRms = -120.0f, subPeak = -120.0f, subRms = -120.0f, outPeak = -120.0f,
              outRms = -120.0f, compGr = 0.0f, limGr = 0.0f;
        bool compOn = false, limOn = false, clip = false;
    };

    void setValues(const Values& v);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override; // clears the clip light
    std::function<void()> onClipReset;

private:
    Values values;
};
} // namespace subgen::ui
