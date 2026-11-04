// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "params/Parameters.h"

namespace subgen::plugin
{
class ParamUndo;
}

namespace subgen::ui
{
// One parameter as a control sees it: host gestures through a ParameterAttachment, each gesture recorded for undo
// once it ends, the value as text with its unit. Message thread.
class ParamBinding
{
public:
    ParamBinding(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id,
                 std::function<void()> onChange);

    params::ParamId id() const { return paramId; }
    const params::ParamSpec& spec() const { return params::spec(paramId); }
    juce::RangedAudioParameter& parameter() { return param; }
    float plain() const;
    float normalised() const { return param.getValue(); }
    float defaultNormalised() const { return param.getDefaultValue(); }
    juce::String text() const;
    juce::String textFor(float plainValue) const;

    void beginGesture();
    void setNormalisedInGesture(float v);
    void endGesture();
    void setPlainAsGesture(float plainValue);
    void resetToDefault();

private:
    void record(float before);

    params::ParamId paramId;
    juce::RangedAudioParameter& param;
    plugin::ParamUndo* undo;
    juce::ParameterAttachment attachment;
    float gestureStart = 0.0f;
    bool inGesture = false;
};

// The base of every control: a binding, a name, dimming when its section is off, a tooltip with the range.
class ParamControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamControl(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id);

    ParamBinding& binding() { return bind; }
    void setLabel(const juce::String& text);
    const juce::String& label() const { return name; }
    void setDimmed(bool dim);
    bool dimmed() const { return isDimmed; }

protected:
    virtual void valueChanged() { repaint(); }
    void updateTooltip();
    float alpha() const { return isDimmed ? 0.38f : 1.0f; }

    juce::String name;
    ParamBinding bind;
    bool isDimmed = false;
};

// A rotary knob: vertical drag (Shift: 1/10), double-click or Alt-click resets, the wheel steps, the right button
// offers "Enter value…" and "Reset to default", the arrow keys step when focused.
class Knob : public ParamControl
{
public:
    enum class Size
    {
        Small,
        Medium,
        Hero
    };

    Knob(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id, Size size);

    static juce::Point<int> cellSize(Size size);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    bool keyPressed(const juce::KeyPress& key) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    void showTextEntry();

private:
    juce::Rectangle<float> dialBounds() const;
    bool bipolar() const;
    void step(float amount);

    Size size;
    float dragValue = 0.0f;
    float lastY = 0.0f;
    bool dragging = false;
    std::unique_ptr<juce::TextEditor> entry;
};

// A choice as a row of segments (a few short labels)
class Segmented : public ParamControl
{
public:
    Segmented(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id, bool showName);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::Rectangle<float> barBounds() const;
    int segmentAt(juce::Point<float> p) const;

    juce::StringArray labels;
    bool withName;
};

// A choice as a box opening a menu
class ChoiceBox : public ParamControl
{
public:
    ChoiceBox(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id, bool showName);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void showMenu();

private:
    juce::Rectangle<float> boxBounds() const;

    juce::StringArray labels;
    bool withName;
};

// A bool as a switch with a light
class Toggle : public ParamControl
{
public:
    Toggle(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id, bool showName);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void flip();

private:
    bool withName;
};

// The control a parameter gets in a grid cell: a knob for a float, segments for a short choice, a box for a longer
// one, a switch for a bool.
std::unique_ptr<ParamControl> makeControl(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo,
                                          params::ParamId id, Knob::Size knobSize = Knob::Size::Small);

// A plain clickable text button in the editor's style
class FlatButton : public juce::Button
{
public:
    explicit FlatButton(const juce::String& text) : juce::Button(text) {}
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    bool accent = false;
};

// The editor's look for menus, text entry and tooltips
class SubGenLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SubGenLookAndFeel();
    juce::Font getPopupMenuFont() override;
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
};
} // namespace subgen::ui
