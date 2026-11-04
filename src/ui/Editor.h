// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/Controls.h"
#include "ui/Display.h"
#include "ui/Spectrum.h"
#include "ui/Theme.h"
#include "ui/UiLayout.h"

namespace subgen::plugin
{
class SubGenProcessor;
}

namespace subgen::ui
{
// One Advanced page: its groups side by side, each a grid of cells three rows high; a group that starts with an
// on/off switch dims the rest while off. The Engine page shows the active engine's groups only.
class AdvancedPanel : public juce::Component
{
public:
    AdvancedPanel(plugin::SubGenProcessor& processor, int page, int engine);
    void paint(juce::Graphics& g) override;
    void updateDimming();
    int page() const { return pageIndex; }
    int engine() const { return engineIndex; }

private:
    struct Placed
    {
        const char* title;
        juce::Rectangle<int> area;
        std::vector<ParamControl*> controls;
        bool firstIsSwitch = false;
    };

    int pageIndex, engineIndex;
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<Placed> groups;
    std::unique_ptr<FlatButton> autoDelay;
};

// The power switch of the top bar: the plug-in on (lit) or bypassed
class PowerSwitch : public ParamControl
{
public:
    PowerSwitch(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
};

// An icon button (undo, redo)
class IconButton : public juce::Button
{
public:
    enum class Icon
    {
        Undo,
        Redo
    };
    IconButton(const juce::String& name, Icon i) : juce::Button(name), icon(i) {}
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon;
};

// The SAM SUB GEN editor (docs/superpowers/specs/2026-11-04-subgen-ui-design.md): the top bar, the analyser and meters,
// the four main sections, the Advanced drawer. 1000 x 620 at 100 %, scaled 75-200 % at a fixed aspect ratio.
class SubGenEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SubGenEditor(plugin::SubGenProcessor& processor);
    ~SubGenEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    void setUiScale(double scale);
    double uiScale() const { return scale; }
    void showAdvancedPage(int page, bool animate = true); // -1: closed
    int advancedPage() const { return openPage; }
    void pollFeedForTest() { timerCallback(); }

private:
    class Content : public juce::Component
    {
    public:
        explicit Content(SubGenEditor& e) : editor(e) {}
        void paint(juce::Graphics& g) override;
        SubGenEditor& editor;
    };

    void timerCallback() override;
    void readFeed();
    void updateContext();
    void updateTopBar();
    void layoutMain();
    void showScaleMenu();
    void startAutoAlign();
    juce::String warningText(juce::String& all) const;
    ParamControl* control(params::ParamId id);

    plugin::SubGenProcessor& proc;
    SubGenLookAndFeel lookAndFeel;
    Content content{*this};
    double scale = 1.0;
    double sampleRate = 48000.0;

    // top bar
    IconButton undoButton{"Undo", IconButton::Icon::Undo}, redoButton{"Redo", IconButton::Icon::Redo};
    std::unique_ptr<Segmented> quality;
    std::unique_ptr<PowerSwitch> power;
    FlatButton scaleButton{"100 %"};
    juce::String warning, warningAll;

    // analyser and meters
    Display display;
    Meters meters;
    std::array<Spectrum, 3> spectra; // dry, sub, sum
    std::array<Display::Levels, 3> shown{};
    std::array<std::vector<float>, 4> lanes;
    Meters::Values meterValues;
    float inMs = 0.0f, subMs = 0.0f, outMs = 0.0f;
    double clipUntil = 0.0;
    double lastTick = 0.0;

    // main sections
    std::vector<std::unique_ptr<ParamControl>> mainControls;
    FlatButton autoAlign{"Auto-align"};
    juce::String alignStatus;
    int alignCountAtRequest = -1;
    juce::String engineText;

    // the Advanced drawer
    std::vector<std::unique_ptr<FlatButton>> tabs;
    std::unique_ptr<AdvancedPanel> panel;
    int openPage = -1;

    juce::TooltipWindow tooltips{this, 700};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SubGenEditor)
};
} // namespace subgen::ui
