// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Controls.h"

#include <cmath>

#include "plugin/ParamUndo.h"
#include "ui/Format.h"
#include "ui/Theme.h"

namespace subgen::ui
{
namespace
{
using params::ParamType;

constexpr float kStartAngle = -0.75f * juce::MathConstants<float>::pi; // 7:30
constexpr float kEndAngle = 0.75f * juce::MathConstants<float>::pi;    // 4:30

juce::String displayName(params::ParamId id)
{
    juce::String n = juce::String::fromUTF8(params::spec(id).name);
    return n.replace(" -", juce::String::fromUTF8(" \xe2\x88\x92")); // "Sub −1 Level"
}

juce::StringArray labelsOf(params::ParamId id)
{
    return juce::StringArray::fromTokens(juce::String::fromUTF8(params::spec(id).choices), "|", "");
}

juce::String rangeText(params::ParamId id)
{
    const auto& s = params::spec(id);
    if (s.type == ParamType::Float)
        return formatValue(id, s.min) + " to " + formatValue(id, s.max) + ", default " + formatValue(id, s.def);
    return labelsOf(id).joinIntoString(", ");
}

// ranged accessibility for a knob, a choice or a switch: the plain value and its text
class ValueInterface : public juce::AccessibilityValueInterface
{
public:
    explicit ValueInterface(ParamBinding& b) : bind(b) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return bind.plain(); }
    juce::String getCurrentValueAsString() const override { return bind.text(); }
    void setValue(double v) override { bind.setPlainAsGesture(static_cast<float>(v)); }
    void setValueAsString(const juce::String& text) override
    {
        if (const auto v = parseValue(bind.id(), text))
            bind.setPlainAsGesture(static_cast<float>(*v));
    }
    AccessibleValueRange getRange() const override
    {
        const auto& s = bind.spec();
        const double interval = s.type == ParamType::Float ? 0.0 : 1.0;
        return AccessibleValueRange({s.min, s.max}, interval);
    }

private:
    ParamBinding& bind;
};

std::unique_ptr<juce::AccessibilityHandler> handlerFor(juce::Component& c, ParamBinding& b, juce::AccessibilityRole role,
                                                       juce::AccessibilityActions actions = {})
{
    return std::make_unique<juce::AccessibilityHandler>(c, role, std::move(actions),
                                                        juce::AccessibilityHandler::Interfaces{std::make_unique<ValueInterface>(b)});
}

void drawName(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, float alpha)
{
    g.setColour(colours::textDim.withMultipliedAlpha(alpha));
    g.setFont(uiFont(11.5f));
    g.drawFittedText(text, area.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------- binding

ParamBinding::ParamBinding(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undoHistory, params::ParamId id,
                           std::function<void()> onChange)
    : paramId(id),
      param(*state.getParameter(params::spec(id).id)),
      undo(undoHistory),
      attachment(param, [cb = std::move(onChange)](float) { if (cb) cb(); })
{
}

float ParamBinding::plain() const { return param.convertFrom0to1(param.getValue()); }

juce::String ParamBinding::text() const { return formatValue(paramId, plain()); }

juce::String ParamBinding::textFor(float plainValue) const { return formatValue(paramId, plainValue); }

void ParamBinding::beginGesture()
{
    if (inGesture)
        return;
    inGesture = true;
    gestureStart = param.getValue();
    attachment.beginGesture();
}

void ParamBinding::setNormalisedInGesture(float v)
{
    attachment.setValueAsPartOfGesture(param.convertFrom0to1(juce::jlimit(0.0f, 1.0f, v)));
}

void ParamBinding::endGesture()
{
    if (!inGesture)
        return;
    inGesture = false;
    attachment.endGesture();
    record(gestureStart);
}

void ParamBinding::setPlainAsGesture(float plainValue)
{
    const float before = param.getValue();
    const auto& s = spec();
    attachment.setValueAsCompleteGesture(juce::jlimit(static_cast<float>(s.min), static_cast<float>(s.max), plainValue));
    record(before);
}

void ParamBinding::resetToDefault() { setPlainAsGesture(param.convertFrom0to1(param.getDefaultValue())); }

void ParamBinding::record(float before)
{
    if (undo != nullptr)
        undo->recordChange(spec().id, before, param.getValue());
}

// ---------------------------------------------------------------------------------------------------------- control

ParamControl::ParamControl(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id)
    : name(displayName(id)), bind(state, undo, id, [this] { valueChanged(); updateTooltip(); })
{
    setWantsKeyboardFocus(true);
    setTitle(name);
    updateTooltip();
}

void ParamControl::setLabel(const juce::String& text)
{
    name = text;
    setTitle(text);
    updateTooltip();
    repaint();
}

void ParamControl::setDimmed(bool dim)
{
    if (dim != isDimmed)
    {
        isDimmed = dim;
        repaint();
    }
}

void ParamControl::updateTooltip()
{
    setTooltip(juce::String::fromUTF8(params::spec(bind.id()).name) + ": " + bind.text() + "\n" + rangeText(bind.id()));
}

// ---------------------------------------------------------------------------------------------------------- knob

Knob::Knob(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id, Size s)
    : ParamControl(state, undo, id), size(s)
{
    setRepaintsOnMouseActivity(false);
}

juce::Point<int> Knob::cellSize(Size s)
{
    switch (s)
    {
    case Size::Small: return {76, 86};
    case Size::Medium: return {92, 108};
    case Size::Hero: return {176, 206};
    }
    return {76, 86};
}

juce::Rectangle<float> Knob::dialBounds() const
{
    const float d = size == Size::Hero ? 150.0f : size == Size::Medium ? 58.0f : 42.0f;
    const auto b = getLocalBounds().toFloat();
    return {b.getCentreX() - d / 2.0f, b.getY() + (size == Size::Hero ? 4.0f : 2.0f), d, d};
}

bool Knob::bipolar() const
{
    const auto& s = bind.spec();
    return s.min < 0.0 && s.max > 0.0 && s.scale != params::Scale::LinDbOff;
}

void Knob::resized()
{
    if (entry != nullptr)
        entry->setBounds(getLocalBounds().removeFromBottom(18).reduced(2, 0));
}

void Knob::paint(juce::Graphics& g)
{
    const float a = alpha();
    const auto dial = dialBounds();
    const float norm = bind.normalised();
    const bool hero = size == Size::Hero;
    const float thick = hero ? 7.0f : size == Size::Medium ? 3.5f : 3.0f;
    const auto centre = dial.getCentre();
    const float radius = dial.getWidth() / 2.0f - thick;
    const float valueAngle = kStartAngle + norm * (kEndAngle - kStartAngle);
    float fromAngle = kStartAngle;
    if (bipolar()) // the arc grows from 0
        fromAngle = kStartAngle + bind.parameter().convertTo0to1(0.0f) * (kEndAngle - kStartAngle);

    // track
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, kStartAngle, kEndAngle, true);
    g.setColour(colours::track.withMultipliedAlpha(a));
    g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, std::min(fromAngle, valueAngle), std::max(fromAngle, valueAngle), true);
    const auto arcColour = hero ? heat(norm) : colours::text.withAlpha(0.82f);
    if (hero && norm > 0.001f) // the glow: wide, faint strokes under the arc
        for (int i = 3; i >= 1; --i)
        {
            g.setColour(arcColour.withAlpha(0.07f * a * (0.4f + norm)));
            g.strokePath(arc, juce::PathStrokeType(thick + 6.0f * static_cast<float>(i), juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }
    g.setColour(arcColour.withMultipliedAlpha(a));
    g.strokePath(arc, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body
    const float bodyR = radius - thick * (hero ? 1.6f : 1.4f);
    const auto body = juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(centre);
    g.setGradientFill(juce::ColourGradient(colours::raised.brighter(0.18f).withMultipliedAlpha(a), centre.x, body.getY(),
                                           colours::raised.darker(0.25f).withMultipliedAlpha(a), centre.x, body.getBottom(), false));
    g.fillEllipse(body);
    g.setColour(colours::line.withMultipliedAlpha(a));
    g.drawEllipse(body, 1.0f);

    // pointer
    const float inner = bodyR * (hero ? 0.62f : 0.35f), outer = bodyR * 0.92f;
    const auto dir = juce::Point<float>(std::sin(valueAngle), -std::cos(valueAngle));
    g.setColour((hero ? arcColour : colours::text).withMultipliedAlpha(a));
    g.drawLine(juce::Line<float>(centre + dir * inner, centre + dir * outer), hero ? 3.0f : 2.0f);

    const bool active = dragging || isMouseOverOrDragging() || hasKeyboardFocus(false);
    if (hero)
    {
        g.setColour(colours::text.withMultipliedAlpha(a));
        g.setFont(uiFont(24.0f, true));
        g.drawText(bind.text(), body.reduced(8.0f).withTrimmedBottom(body.getHeight() * 0.18f),
                   juce::Justification::centred);
        auto rest = getLocalBounds().toFloat().withTop(dial.getBottom() + 6.0f);
        g.setColour(colours::text.withMultipliedAlpha(a));
        g.setFont(uiFont(15.0f, true));
        g.drawText(name, rest.removeFromTop(20.0f), juce::Justification::centred);
        return;
    }

    auto rest = getLocalBounds().toFloat().withTop(dial.getBottom() + 3.0f);
    drawName(g, rest.removeFromTop(15.0f), name, a);
    if (entry == nullptr)
    {
        g.setColour((active ? colours::sub : colours::text).withMultipliedAlpha(a));
        g.setFont(uiFont(12.0f));
        g.drawText(bind.text(), rest.removeFromTop(16.0f), juce::Justification::centred);
    }
    if (hasKeyboardFocus(false))
    {
        g.setColour(colours::sub.withAlpha(0.5f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.0f);
    }
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addSectionHeader(juce::String::fromUTF8(params::spec(bind.id()).name) + ": " + bind.text());
        menu.addItem(1, juce::String::fromUTF8("Enter value\xe2\x80\xa6"));
        menu.addItem(2, "Reset to default");
        juce::Component::SafePointer<Knob> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int r) {
            if (safe == nullptr)
                return;
            if (r == 1)
                safe->showTextEntry();
            else if (r == 2)
                safe->bind.resetToDefault();
        });
        return;
    }
    if (e.mods.isAltDown())
    {
        bind.resetToDefault();
        return;
    }
    dragging = true;
    dragValue = bind.normalised();
    lastY = e.position.y;
    bind.beginGesture();
    repaint();
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging)
        return;
    const float pixels = (lastY - e.position.y) + 0.0f;
    lastY = e.position.y;
    const float range = size == Size::Hero ? 320.0f : 220.0f; // pixels for the whole range
    dragValue = juce::jlimit(0.0f, 1.0f, dragValue + pixels / range * (e.mods.isShiftDown() ? 0.1f : 1.0f));
    bind.setNormalisedInGesture(dragValue);
}

void Knob::mouseUp(const juce::MouseEvent&)
{
    if (!dragging)
        return;
    dragging = false;
    bind.endGesture();
    repaint();
}

void Knob::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu())
        bind.resetToDefault();
}

void Knob::step(float amount)
{
    bind.setPlainAsGesture(bind.parameter().convertFrom0to1(juce::jlimit(0.0f, 1.0f, bind.normalised() + amount)));
}

void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float delta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (delta == 0.0f)
        return;
    step((delta > 0.0f ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.002f : 0.01f));
}

bool Knob::keyPressed(const juce::KeyPress& key)
{
    const bool fine = key.getModifiers().isShiftDown();
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::rightKey))
        step(fine ? 0.002f : 0.01f);
    else if (key.isKeyCode(juce::KeyPress::downKey) || key.isKeyCode(juce::KeyPress::leftKey))
        step(fine ? -0.002f : -0.01f);
    else if (key.isKeyCode(juce::KeyPress::returnKey))
        showTextEntry();
    else
        return false;
    return true;
}

void Knob::showTextEntry()
{
    entry = std::make_unique<juce::TextEditor>();
    entry->setJustification(juce::Justification::centred);
    entry->setFont(uiFont(12.0f));
    entry->setColour(juce::TextEditor::backgroundColourId, colours::background);
    entry->setColour(juce::TextEditor::textColourId, colours::text);
    entry->setColour(juce::TextEditor::outlineColourId, colours::sub);
    entry->setColour(juce::TextEditor::focusedOutlineColourId, colours::sub);
    entry->setText(bind.text(), false);
    entry->selectAll();
    juce::Component::SafePointer<Knob> safe(this);
    auto finish = [safe](bool apply) {
        if (safe == nullptr || safe->entry == nullptr)
            return;
        if (apply)
            if (const auto v = parseValue(safe->bind.id(), safe->entry->getText()))
                safe->bind.setPlainAsGesture(static_cast<float>(*v));
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr)
            {
                safe->entry.reset();
                safe->repaint();
            }
        });
    };
    entry->onReturnKey = [finish] { finish(true); };
    entry->onEscapeKey = [finish] { finish(false); };
    entry->onFocusLost = [finish] { finish(true); };
    addAndMakeVisible(*entry);
    resized();
    entry->grabKeyboardFocus();
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> Knob::createAccessibilityHandler()
{
    return handlerFor(*this, bind, juce::AccessibilityRole::slider);
}

// ---------------------------------------------------------------------------------------------------------- segments

Segmented::Segmented(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id, bool showName)
    : ParamControl(state, undo, id), labels(labelsOf(id)), withName(showName)
{
}

juce::Rectangle<float> Segmented::barBounds() const
{
    auto b = getLocalBounds().toFloat();
    if (withName)
        b.removeFromTop(17.0f);
    return b.withSizeKeepingCentre(b.getWidth(), std::min(b.getHeight(), 26.0f)).reduced(1.0f);
}

int Segmented::segmentAt(juce::Point<float> p) const
{
    const auto bar = barBounds();
    if (!bar.contains(p) || labels.isEmpty())
        return -1;
    return juce::jlimit(0, labels.size() - 1, static_cast<int>((p.x - bar.getX()) / bar.getWidth() * static_cast<float>(labels.size())));
}

void Segmented::paint(juce::Graphics& g)
{
    const float a = alpha();
    if (withName)
        drawName(g, getLocalBounds().toFloat().removeFromTop(16.0f), name, a);
    const auto bar = barBounds();
    g.setColour(colours::background.withMultipliedAlpha(a));
    g.fillRoundedRectangle(bar, 5.0f);
    g.setColour(colours::line.withMultipliedAlpha(a));
    g.drawRoundedRectangle(bar, 5.0f, 1.0f);
    const int selected = static_cast<int>(std::lround(bind.plain()));
    const float w = bar.getWidth() / static_cast<float>(std::max(1, labels.size()));
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto seg = juce::Rectangle<float>(bar.getX() + w * static_cast<float>(i), bar.getY(), w, bar.getHeight());
        if (i == selected)
        {
            g.setColour(colours::raised.brighter(0.25f).withMultipliedAlpha(a));
            g.fillRoundedRectangle(seg.reduced(2.0f), 4.0f);
        }
        g.setColour((i == selected ? colours::text : colours::textDim).withMultipliedAlpha(a));
        g.setFont(uiFont(11.5f, i == selected));
        g.drawFittedText(labels[i], seg.reduced(2.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }
    if (hasKeyboardFocus(false))
    {
        g.setColour(colours::sub.withAlpha(0.5f));
        g.drawRoundedRectangle(bar.expanded(1.0f), 5.0f, 1.0f);
    }
}

void Segmented::mouseDown(const juce::MouseEvent& e)
{
    const int i = segmentAt(e.position);
    if (i >= 0)
        bind.setPlainAsGesture(static_cast<float>(i));
}

bool Segmented::keyPressed(const juce::KeyPress& key)
{
    const int now = static_cast<int>(std::lround(bind.plain()));
    if (key.isKeyCode(juce::KeyPress::rightKey))
        bind.setPlainAsGesture(static_cast<float>(std::min(labels.size() - 1, now + 1)));
    else if (key.isKeyCode(juce::KeyPress::leftKey))
        bind.setPlainAsGesture(static_cast<float>(std::max(0, now - 1)));
    else
        return false;
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> Segmented::createAccessibilityHandler()
{
    return handlerFor(*this, bind, juce::AccessibilityRole::slider);
}

// ---------------------------------------------------------------------------------------------------------- choice box

ChoiceBox::ChoiceBox(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id, bool showName)
    : ParamControl(state, undo, id), labels(labelsOf(id)), withName(showName)
{
}

juce::Rectangle<float> ChoiceBox::boxBounds() const
{
    auto b = getLocalBounds().toFloat();
    if (withName)
        b.removeFromTop(17.0f);
    return b.withSizeKeepingCentre(b.getWidth(), std::min(b.getHeight(), 26.0f)).reduced(1.0f);
}

void ChoiceBox::paint(juce::Graphics& g)
{
    const float a = alpha();
    if (withName)
        drawName(g, getLocalBounds().toFloat().removeFromTop(16.0f), name, a);
    const auto box = boxBounds();
    g.setColour(colours::raised.withMultipliedAlpha(a));
    g.fillRoundedRectangle(box, 5.0f);
    g.setColour((hasKeyboardFocus(false) ? colours::sub.withAlpha(0.6f) : colours::line).withMultipliedAlpha(a));
    g.drawRoundedRectangle(box, 5.0f, 1.0f);
    auto text = box.reduced(7.0f, 0.0f);
    const auto chevronArea = text.removeFromRight(9.0f);
    text.removeFromRight(2.0f);
    g.setColour(colours::text.withMultipliedAlpha(a));
    g.setFont(uiFont(12.0f));
    g.drawFittedText(bind.text(), text.toNearestInt(), juce::Justification::centredLeft, 1, 0.68f);
    juce::Path chevron;
    const auto c = chevronArea.getCentre();
    chevron.startNewSubPath(c.x - 3.5f, c.y - 1.5f);
    chevron.lineTo(c.x, c.y + 2.0f);
    chevron.lineTo(c.x + 3.5f, c.y - 1.5f);
    g.setColour(colours::textDim.withMultipliedAlpha(a));
    g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ChoiceBox::showMenu()
{
    juce::PopupMenu menu;
    const int selected = static_cast<int>(std::lround(bind.plain()));
    for (int i = 0; i < labels.size(); ++i)
        menu.addItem(i + 1, labels[i], true, i == selected);
    juce::Component::SafePointer<ChoiceBox> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()), [safe](int r) {
        if (safe != nullptr && r > 0)
            safe->bind.setPlainAsGesture(static_cast<float>(r - 1));
    });
}

void ChoiceBox::mouseDown(const juce::MouseEvent& e)
{
    if (boxBounds().contains(e.position))
        showMenu();
}

bool ChoiceBox::keyPressed(const juce::KeyPress& key)
{
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey))
    {
        showMenu();
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> ChoiceBox::createAccessibilityHandler()
{
    return handlerFor(*this, bind, juce::AccessibilityRole::comboBox,
                      juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] { showMenu(); }));
}

// ---------------------------------------------------------------------------------------------------------- toggle

Toggle::Toggle(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo, params::ParamId id, bool showName)
    : ParamControl(state, undo, id), withName(showName)
{
}

void Toggle::paint(juce::Graphics& g)
{
    const float a = alpha();
    auto b = getLocalBounds().toFloat();
    if (withName)
        drawName(g, b.removeFromTop(17.0f), name, a);
    const auto pill = b.withSizeKeepingCentre(b.getWidth(), std::min(b.getHeight(), 26.0f)).reduced(1.0f);
    const bool on = bind.normalised() >= 0.5f;
    g.setColour((on ? colours::raised.brighter(0.15f) : colours::background).withMultipliedAlpha(a));
    g.fillRoundedRectangle(pill, pill.getHeight() / 2.0f);
    g.setColour((hasKeyboardFocus(false) ? colours::sub.withAlpha(0.6f) : colours::line).withMultipliedAlpha(a));
    g.drawRoundedRectangle(pill, pill.getHeight() / 2.0f, 1.0f);
    auto inner = pill.reduced(9.0f, 0.0f);
    const auto led = inner.removeFromLeft(8.0f).withSizeKeepingCentre(7.0f, 7.0f);
    if (on)
    {
        g.setColour(colours::sub.withAlpha(0.25f * a));
        g.fillEllipse(led.expanded(3.0f));
    }
    g.setColour((on ? colours::sub : colours::textFaint).withMultipliedAlpha(a));
    g.fillEllipse(led);
    inner.removeFromLeft(6.0f);
    g.setColour((on ? colours::text : colours::textDim).withMultipliedAlpha(a));
    g.setFont(uiFont(11.5f, on));
    // a switch named in its cell shows its state; one without a name shows the name
    const auto text = withName ? bind.text() : name;
    g.drawFittedText(text.substring(0, 1).toUpperCase() + text.substring(1), inner.toNearestInt(),
                     juce::Justification::centredLeft, 1, 0.75f);
}

void Toggle::flip() { bind.setPlainAsGesture(bind.normalised() >= 0.5f ? 0.0f : 1.0f); }

void Toggle::mouseDown(const juce::MouseEvent&) { flip(); }

bool Toggle::keyPressed(const juce::KeyPress& key)
{
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey))
    {
        flip();
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> Toggle::createAccessibilityHandler()
{
    return handlerFor(*this, bind, juce::AccessibilityRole::toggleButton,
                      juce::AccessibilityActions().addAction(juce::AccessibilityActionType::toggle, [this] { flip(); }));
}

// ---------------------------------------------------------------------------------------------------------- factory

std::unique_ptr<ParamControl> makeControl(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo,
                                          params::ParamId id, Knob::Size knobSize)
{
    const auto& s = params::spec(id);
    switch (s.type)
    {
    case ParamType::Float: return std::make_unique<Knob>(state, undo, id, knobSize);
    case ParamType::Bool: return std::make_unique<Toggle>(state, undo, id, true);
    case ParamType::Choice:
    {
        const auto labels = labelsOf(id);
        int longest = 0;
        for (const auto& l : labels)
            longest = std::max(longest, l.length());
        if (labels.size() <= 2 && longest <= 6)
            return std::make_unique<Segmented>(state, undo, id, true);
        return std::make_unique<ChoiceBox>(state, undo, id, true);
    }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------- misc

void FlatButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    const bool on = getToggleState();
    g.setColour((down ? colours::raised.brighter(0.3f) : on || highlighted ? colours::raised.brighter(0.15f) : colours::raised)
                    .withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(accent ? colours::sub.withAlpha(0.7f) : colours::line);
    g.drawRoundedRectangle(b, 5.0f, 1.0f);
    g.setColour((accent ? colours::sub : colours::text).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
    g.setFont(uiFont(12.0f, accent));
    g.drawFittedText(getButtonText(), b.reduced(6.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

SubGenLookAndFeel::SubGenLookAndFeel()
{
    setColour(juce::PopupMenu::backgroundColourId, colours::raised);
    setColour(juce::PopupMenu::textColourId, colours::text);
    setColour(juce::PopupMenu::headerTextColourId, colours::textDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::sub.withAlpha(0.22f));
    setColour(juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour(juce::TooltipWindow::backgroundColourId, colours::raised);
    setColour(juce::TooltipWindow::textColourId, colours::text);
    setColour(juce::TooltipWindow::outlineColourId, colours::line);
    setColour(juce::TextEditor::highlightColourId, colours::sub.withAlpha(0.35f));
    setColour(juce::CaretComponent::caretColourId, colours::sub);
}

juce::Font SubGenLookAndFeel::getPopupMenuFont() { return uiFont(13.0f); }

juce::Rectangle<int> SubGenLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.append(tipText, uiFont(12.0f), colours::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths(s, 320.0f);
    const int w = static_cast<int>(layout.getWidth() + 20.0f), h = static_cast<int>(layout.getHeight() + 14.0f);
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin(parentArea);
}

void SubGenLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto b = juce::Rectangle<int>(width, height).toFloat();
    g.setColour(colours::raised);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(colours::line);
    g.drawRoundedRectangle(b.reduced(0.5f), 5.0f, 1.0f);
    juce::AttributedString s;
    s.append(text, uiFont(12.0f), colours::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths(s, 320.0f);
    layout.draw(g, b.reduced(10.0f, 7.0f));
}
} // namespace subgen::ui
