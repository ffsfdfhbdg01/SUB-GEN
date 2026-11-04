// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Display.h"

#include <cmath>

#include "ui/Format.h"
#include "ui/Theme.h"

namespace subgen::ui
{
namespace
{
using params::ParamId;

constexpr double kLoHz = 10.0, kHiHz = 500.0;
constexpr float kTopDb = 0.0f, kBottomDb = -90.0f;
constexpr double kEqRangeDb = 18.0;

constexpr std::array<ParamId, 5> kEqOn{ParamId::eq1_on, ParamId::eq2_on, ParamId::eq3_on, ParamId::eq4_on, ParamId::eq5_on};
constexpr std::array<ParamId, 5> kEqType{ParamId::eq1_type, ParamId::eq2_type, ParamId::eq3_type, ParamId::eq4_type,
                                         ParamId::eq5_type};
constexpr std::array<ParamId, 5> kEqFreq{ParamId::eq1_freq, ParamId::eq2_freq, ParamId::eq3_freq, ParamId::eq4_freq,
                                         ParamId::eq5_freq};
constexpr std::array<ParamId, 5> kEqGain{ParamId::eq1_gain, ParamId::eq2_gain, ParamId::eq3_gain, ParamId::eq4_gain,
                                         ParamId::eq5_gain};
constexpr std::array<ParamId, 5> kEqQ{ParamId::eq1_q, ParamId::eq2_q, ParamId::eq3_q, ParamId::eq4_q, ParamId::eq5_q};

juce::String hzLabel(double hz) { return juce::String(static_cast<int>(hz)); }
} // namespace

Display::Display(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo)
{
    auto bind = [&](ParamId id) { return std::make_unique<ParamBinding>(state, undo, id, [this] { bindChanged(); }); };
    rangeLo = bind(ParamId::range_lo);
    rangeHi = bind(ParamId::range_hi);
    for (size_t b = 0; b < eq.size(); ++b)
        eq[b] = {bind(kEqOn[b]), bind(kEqType[b]), bind(kEqFreq[b]), bind(kEqGain[b]), bind(kEqQ[b])};
    dry.fill(Spectrum::kFloorDb);
    sub.fill(Spectrum::kFloorDb);
    sum.fill(Spectrum::kFloorDb);
    setTitle("Analyser");
    setDescription("Spectrum of the dry, the sub and the sum, 10 to 500 Hz; drag the source band's edges");
}

void Display::setLevels(const Levels& d, const Levels& s, const Levels& m)
{
    dry = d;
    sub = s;
    sum = m;
    repaint();
}

void Display::setPitch(double hz, bool sounding)
{
    if (hz != pitchHz || sounding != pitchSounding)
    {
        pitchHz = hz;
        pitchSounding = sounding;
        repaint();
    }
}

void Display::setSampleRate(double rate) { fs = rate > 0.0 ? rate : 48000.0; }

void Display::setEqVisible(bool visible)
{
    showEq = visible;
    repaint();
}

juce::Rectangle<float> Display::plot() const { return getLocalBounds().toFloat().reduced(14.0f, 0.0f).withTrimmedTop(10.0f).withTrimmedBottom(22.0f); }

float Display::xFor(double hz) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * static_cast<float>(std::log(hz / kLoHz) / std::log(kHiHz / kLoHz));
}

double Display::hzFor(float x) const
{
    const auto p = plot();
    return kLoHz * std::pow(kHiHz / kLoHz, static_cast<double>((x - p.getX()) / p.getWidth()));
}

float Display::yForDb(float db) const
{
    const auto p = plot();
    return juce::jmap(juce::jlimit(kBottomDb, kTopDb, db), kTopDb, kBottomDb, p.getY(), p.getBottom());
}

float Display::yForEqDb(double db) const
{
    const auto p = plot().reduced(0.0f, 14.0f);
    return juce::jmap(static_cast<float>(juce::jlimit(-kEqRangeDb, kEqRangeDb, db)), static_cast<float>(kEqRangeDb),
                      static_cast<float>(-kEqRangeDb), p.getY(), p.getBottom());
}

double Display::eqDbFor(float y) const
{
    const auto p = plot().reduced(0.0f, 14.0f);
    return juce::jmap(static_cast<double>(y), static_cast<double>(p.getY()), static_cast<double>(p.getBottom()), kEqRangeDb,
                      -kEqRangeDb);
}

juce::Point<float> Display::nodePosition(int band) const
{
    const auto& b = eq[static_cast<size_t>(band)];
    return {xFor(b.freq->plain()), yForEqDb(b.gain->plain())};
}

std::pair<Display::Target, int> Display::hit(juce::Point<float> p) const
{
    if (showEq)
        for (int b = 4; b >= 0; --b)
            if (nodePosition(b).getDistanceFrom(p) < 11.0f)
                return {Target::EqNode, b};
    const float lo = xFor(rangeLo->plain()), hi = xFor(rangeHi->plain());
    if (std::abs(p.x - lo) < 7.0f && std::abs(p.x - lo) <= std::abs(p.x - hi))
        return {Target::RangeLo, -1};
    if (std::abs(p.x - hi) < 7.0f)
        return {Target::RangeHi, -1};
    return {Target::None, -1};
}

void Display::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colours::background.darker(0.25f));
    g.fillRect(bounds);
    const auto p = plot();
    drawGrid(g, p);
    drawRange(g, p);
    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(p.toNearestInt());
        drawSpectrum(g, dry, colours::dry, false, 1.3f);
        drawSpectrum(g, sub, colours::sub, true, 1.8f);
        drawSpectrum(g, sum, colours::sum.withAlpha(0.75f), false, 1.0f);
    }
    if (showEq)
        drawEq(g, p);

    // the pitch of Sub −1
    auto info = p.reduced(12.0f, 8.0f).removeFromTop(40.0f).removeFromLeft(220.0f);
    const float a = pitchSounding ? 1.0f : 0.4f;
    g.setFont(uiFont(11.5f));
    g.setColour(colours::textDim.withMultipliedAlpha(a));
    g.drawText(juce::String::fromUTF8("Sub \xe2\x88\x92" "1"), info.removeFromTop(14.0f), juce::Justification::centredLeft);
    auto line = info.removeFromTop(22.0f);
    g.setFont(uiFont(19.0f, true));
    g.setColour(colours::sub.withMultipliedAlpha(a));
    const auto hzText = pitchHz > 0.0 ? juce::String(pitchHz, 1) + " Hz" : juce::String::fromUTF8("\xe2\x80\x94");
    g.drawText(hzText, line, juce::Justification::centredLeft);
    const float used = juce::GlyphArrangement::getStringWidth(uiFont(19.0f, true), hzText);
    g.setFont(uiFont(12.5f));
    g.setColour(colours::textDim.withMultipliedAlpha(a));
    g.drawText(noteName(pitchHz), line.withTrimmedLeft(used + 10.0f), juce::Justification::centredLeft);

    // the legend
    auto legend = juce::Rectangle<float>(p.getRight() - 212.0f, p.getY() + 10.0f, 160.0f, 14.0f);
    const std::array<std::pair<const char*, juce::Colour>, 3> keys{{{"Dry", colours::dry}, {"Sub", colours::sub}, {"Sum", colours::sum}}};
    for (const auto& [label, colour] : keys)
    {
        auto item = legend.removeFromLeft(53.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(item.removeFromLeft(12.0f).withSizeKeepingCentre(12.0f, 2.5f), 1.0f);
        g.setColour(colours::textDim);
        g.setFont(uiFont(11.0f));
        g.drawText(label, item.withTrimmedLeft(5.0f), juce::Justification::centredLeft);
    }
}

void Display::drawGrid(juce::Graphics& g, juce::Rectangle<float> p)
{
    g.setFont(uiFont(10.0f));
    for (double hz : {20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0})
    {
        const bool major = hz == 20.0 || hz == 50.0 || hz == 100.0 || hz == 200.0;
        g.setColour(colours::line.withAlpha(major ? 0.55f : 0.25f));
        const float x = xFor(hz);
        g.drawVerticalLine(juce::roundToInt(x), p.getY(), p.getBottom());
        if (major || hz == 30.0 || hz == 300.0)
        {
            g.setColour(colours::textFaint);
            g.drawText(hzLabel(hz), juce::Rectangle<float>(x - 20.0f, p.getBottom() + 4.0f, 40.0f, 14.0f), juce::Justification::centred);
        }
    }
    g.setColour(colours::textFaint);
    g.drawText("Hz", juce::Rectangle<float>(p.getRight() - 24.0f, p.getBottom() + 4.0f, 24.0f, 14.0f), juce::Justification::centredRight);
    if (!showEq)
        for (float db = -12.0f; db > kBottomDb; db -= 12.0f)
        {
            const float y = yForDb(db);
            g.setColour(colours::line.withAlpha(0.22f));
            g.drawHorizontalLine(juce::roundToInt(y), p.getX(), p.getRight());
            g.setColour(colours::textFaint);
            g.drawText(juce::String(static_cast<int>(db)).replace("-", juce::String::fromUTF8("\xe2\x88\x92")),
                       juce::Rectangle<float>(p.getRight() - 30.0f, y - 13.0f, 28.0f, 12.0f), juce::Justification::centredRight);
        }
}

void Display::drawSpectrum(juce::Graphics& g, const Levels& levels, juce::Colour colour, bool fill, float width)
{
    const auto p = plot();
    juce::Path path;
    for (int i = 0; i < Spectrum::kPoints; ++i)
    {
        const auto pt = juce::Point<float>(xFor(Spectrum::pointHz(i)), yForDb(levels[static_cast<size_t>(i)]));
        if (i == 0)
            path.startNewSubPath(pt);
        else
            path.lineTo(pt);
    }
    if (fill)
    {
        juce::Path area(path);
        area.lineTo(p.getRight(), p.getBottom());
        area.lineTo(p.getX(), p.getBottom());
        area.closeSubPath();
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.30f), 0.0f, p.getY(), colour.withAlpha(0.02f), 0.0f,
                                               p.getBottom(), false));
        g.fillPath(area);
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Display::drawRange(juce::Graphics& g, juce::Rectangle<float> p)
{
    const float lo = xFor(rangeLo->plain()), hi = xFor(rangeHi->plain());
    g.setColour(colours::dry.withAlpha(0.07f));
    g.fillRect(juce::Rectangle<float>(lo, p.getY(), hi - lo, p.getHeight()));
    for (auto [x, target, binding] : {std::tuple{lo, Target::RangeLo, rangeLo.get()}, std::tuple{hi, Target::RangeHi, rangeHi.get()}})
    {
        const bool active = hover == target || dragging == target;
        g.setColour(colours::dry.withAlpha(active ? 0.9f : 0.45f));
        g.drawVerticalLine(juce::roundToInt(x), p.getY(), p.getBottom());
        const auto tab = juce::Rectangle<float>(46.0f, 16.0f).withCentre({x, p.getBottom() - 12.0f});
        g.setColour(active ? colours::dry.withAlpha(0.95f) : colours::raised);
        g.fillRoundedRectangle(tab, 4.0f);
        g.setColour(active ? colours::background : colours::textDim);
        g.setFont(uiFont(10.5f, active));
        g.drawText(binding->text(), tab, juce::Justification::centred);
    }
    g.setColour(colours::textFaint);
    g.setFont(uiFont(10.5f));
    g.drawText("Source band", juce::Rectangle<float>(lo + 6.0f, p.getBottom() - 42.0f, std::max(70.0f, hi - lo - 12.0f), 14.0f),
               juce::Justification::centredLeft);
}

void Display::drawEq(juce::Graphics& g, juce::Rectangle<float> p)
{
    g.setColour(colours::background.darker(0.25f).withAlpha(0.55f));
    g.fillRect(p);
    for (double db : {-12.0, -6.0, 0.0, 6.0, 12.0})
    {
        const float y = yForEqDb(db);
        g.setColour(colours::line.withAlpha(db == 0.0 ? 0.7f : 0.3f));
        g.drawHorizontalLine(juce::roundToInt(y), p.getX(), p.getRight());
        g.setColour(colours::textFaint);
        g.setFont(uiFont(10.0f));
        g.drawText((db > 0 ? "+" : "") + juce::String(static_cast<int>(db)).replace("-", juce::String::fromUTF8("\xe2\x88\x92")),
                   juce::Rectangle<float>(p.getRight() - 30.0f, y - 13.0f, 28.0f, 12.0f), juce::Justification::centredRight);
    }
    chain::EqParams params;
    for (size_t b = 0; b < eq.size(); ++b)
        params.bands[b] = {eq[b].on->plain() >= 0.5f, static_cast<chain::EqType>(std::lround(eq[b].type->plain())),
                           eq[b].freq->plain(), eq[b].gain->plain(), eq[b].q->plain()};
    eqCurve.prepare(fs); // fresh: the new parameters apply at once, no smoothing
    eqCurve.setParams(params);
    juce::Path curve;
    for (float x = p.getX(); x <= p.getRight(); x += 2.0f)
    {
        const auto pt = juce::Point<float>(x, yForEqDb(eqCurve.curveDb(hzFor(x))));
        if (x == p.getX())
            curve.startNewSubPath(pt);
        else
            curve.lineTo(pt);
    }
    juce::Path area(curve);
    area.lineTo(p.getRight(), yForEqDb(0.0));
    area.lineTo(p.getX(), yForEqDb(0.0));
    area.closeSubPath();
    g.setColour(colours::sub.withAlpha(0.12f));
    g.fillPath(area);
    g.setColour(colours::sub);
    g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int b = 0; b < 5; ++b)
    {
        const auto c = nodePosition(b);
        const bool on = eq[static_cast<size_t>(b)].on->plain() >= 0.5f;
        const bool active = (hover == Target::EqNode && hoverBand == b) || (dragging == Target::EqNode && dragBand == b);
        const float r = active ? 9.0f : 8.0f;
        g.setColour(on ? colours::sub : colours::background);
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));
        g.setColour(on ? colours::sub.brighter(0.4f) : colours::textDim);
        g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c), active ? 1.8f : 1.2f);
        g.setColour(on ? colours::background : colours::textDim);
        g.setFont(uiFont(10.5f, true));
        g.drawText(juce::String(b + 1), juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c), juce::Justification::centred);
        if (active)
        {
            const auto& band = eq[static_cast<size_t>(b)];
            const auto text = band.freq->text() + "   " + band.gain->text() + "   Q " + band.q->text();
            g.setColour(colours::text);
            g.setFont(uiFont(11.5f));
            g.drawText(text, juce::Rectangle<float>(c.x - 90.0f, c.y - 30.0f, 180.0f, 14.0f), juce::Justification::centred);
        }
    }
}

void Display::mouseMove(const juce::MouseEvent& e)
{
    const auto [t, band] = hit(e.position);
    if (t != hover || band != hoverBand)
    {
        hover = t;
        hoverBand = band;
        setMouseCursor(t == Target::None ? juce::MouseCursor::NormalCursor
                       : t == Target::EqNode ? juce::MouseCursor::DraggingHandCursor
                                             : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void Display::mouseDown(const juce::MouseEvent& e)
{
    std::tie(dragging, dragBand) = hit(e.position);
    if (dragging == Target::RangeLo)
        rangeLo->beginGesture();
    else if (dragging == Target::RangeHi)
        rangeHi->beginGesture();
    else if (dragging == Target::EqNode)
    {
        auto& b = eq[static_cast<size_t>(dragBand)];
        if (b.on->plain() < 0.5f)
            b.on->setPlainAsGesture(1.0f);
        b.freq->beginGesture();
        b.gain->beginGesture();
    }
}

void Display::mouseDrag(const juce::MouseEvent& e)
{
    const auto x = juce::jlimit(plot().getX(), plot().getRight(), e.position.x);
    auto toNorm = [](ParamBinding& b, double plain) { return b.parameter().convertTo0to1(static_cast<float>(plain)); };
    if (dragging == Target::RangeLo)
        rangeLo->setNormalisedInGesture(toNorm(*rangeLo, hzFor(x)));
    else if (dragging == Target::RangeHi)
        rangeHi->setNormalisedInGesture(toNorm(*rangeHi, hzFor(x)));
    else if (dragging == Target::EqNode)
    {
        auto& b = eq[static_cast<size_t>(dragBand)];
        b.freq->setNormalisedInGesture(toNorm(*b.freq, hzFor(x)));
        b.gain->setNormalisedInGesture(toNorm(*b.gain, eqDbFor(e.position.y)));
    }
    repaint();
}

void Display::mouseUp(const juce::MouseEvent&)
{
    if (dragging == Target::RangeLo)
        rangeLo->endGesture();
    else if (dragging == Target::RangeHi)
        rangeHi->endGesture();
    else if (dragging == Target::EqNode)
    {
        eq[static_cast<size_t>(dragBand)].freq->endGesture();
        eq[static_cast<size_t>(dragBand)].gain->endGesture();
    }
    dragging = Target::None;
    repaint();
}

void Display::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto [t, band] = hit(e.position);
    if (t == Target::EqNode)
    {
        auto& on = *eq[static_cast<size_t>(band)].on;
        on.setPlainAsGesture(on.plain() >= 0.5f ? 0.0f : 1.0f);
    }
    else if (t == Target::RangeLo)
        rangeLo->resetToDefault();
    else if (t == Target::RangeHi)
        rangeHi->resetToDefault();
}

void Display::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto [t, band] = hit(e.position);
    if (t != Target::EqNode || wheel.deltaY == 0.0f)
        return;
    auto& q = *eq[static_cast<size_t>(band)].q;
    const float step = (wheel.deltaY > 0.0f ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.005f : 0.02f);
    q.setPlainAsGesture(q.parameter().convertFrom0to1(juce::jlimit(0.0f, 1.0f, q.normalised() + step)));
    repaint();
}

// ---------------------------------------------------------------------------------------------------------- meters

void Meters::setValues(const Values& v)
{
    values = v;
    repaint();
}

void Meters::mouseDown(const juce::MouseEvent&)
{
    if (onClipReset)
        onClipReset();
}

void Meters::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(colours::background.darker(0.25f));
    g.fillRect(b);
    g.setColour(colours::line);
    g.drawVerticalLine(0, b.getY(), b.getBottom());
    b = b.reduced(10.0f, 10.0f).withTrimmedBottom(12.0f);
    auto labels = b.removeFromBottom(14.0f);
    auto scale = b.removeFromLeft(22.0f);
    b = b.withTrimmedTop(16.0f);
    const float top = b.getY(), bottom = b.getBottom();
    auto yFor = [&](float db) { return juce::jmap(juce::jlimit(-60.0f, 0.0f, db), 0.0f, -60.0f, top, bottom); };
    g.setFont(uiFont(9.5f));
    for (float db = 0.0f; db >= -60.0f; db -= 12.0f)
    {
        g.setColour(colours::textFaint);
        g.drawText(db == 0.0f ? "0" : juce::String(static_cast<int>(db)).replace("-", juce::String::fromUTF8("\xe2\x88\x92")),
                   juce::Rectangle<float>(scale.getX(), yFor(db) - 6.0f, scale.getWidth() - 3.0f, 12.0f),
                   juce::Justification::centredRight);
    }
    const int bars = 3 + (values.compOn ? 1 : 0) + (values.limOn ? 1 : 0);
    const float w = b.getWidth() / static_cast<float>(bars);
    auto bar = [&](int i, const char* label, float peak, float rms, juce::Colour colour) {
        const auto col = juce::Rectangle<float>(b.getX() + w * static_cast<float>(i), top, w, bottom - top).reduced(w * 0.22f, 0.0f);
        g.setColour(colours::track);
        g.fillRoundedRectangle(col, 2.0f);
        const float yr = yFor(rms);
        g.setColour(colour.withAlpha(0.85f));
        g.fillRoundedRectangle(col.withTop(yr), 2.0f);
        if (peak > -60.0f)
        {
            g.setColour(colour.brighter(0.4f));
            g.fillRect(col.withTop(yFor(peak)).withHeight(2.0f));
        }
        g.setColour(colours::textDim);
        g.setFont(uiFont(10.0f));
        g.drawText(label, juce::Rectangle<float>(col.getCentreX() - w / 2.0f, labels.getY(), w, labels.getHeight()), juce::Justification::centred);
    };
    auto gr = [&](int i, const char* label, float db) {
        const auto col = juce::Rectangle<float>(b.getX() + w * static_cast<float>(i), top, w, bottom - top).reduced(w * 0.22f, 0.0f);
        g.setColour(colours::track);
        g.fillRoundedRectangle(col, 2.0f);
        const float y = juce::jmap(juce::jlimit(0.0f, 24.0f, db), 0.0f, 24.0f, top, bottom);
        g.setColour(colours::ember.withAlpha(0.85f));
        g.fillRoundedRectangle(col.withBottom(y), 2.0f);
        g.setColour(colours::textDim);
        g.setFont(uiFont(10.0f));
        g.drawText(label, juce::Rectangle<float>(col.getCentreX() - w / 2.0f, labels.getY(), w, labels.getHeight()), juce::Justification::centred);
    };
    bar(0, "In", values.inPeak, values.inRms, colours::dry);
    bar(1, "Sub", values.subPeak, values.subRms, colours::sub);
    bar(2, "Out", values.outPeak, values.outRms, colours::sum.withAlpha(0.9f));
    int next = 3;
    if (values.compOn)
        gr(next++, "Cmp", values.compGr);
    if (values.limOn)
        gr(next, "Lim", values.limGr);

    // the output's peak and the clip light (W-06)
    const auto led = juce::Rectangle<float>(b.getX() + w * 2.0f, top - 15.0f, w, 10.0f).reduced(w * 0.22f, 0.0f);
    g.setColour(values.clip ? colours::warning : colours::track);
    g.fillRoundedRectangle(led, 2.0f);
    g.setColour(values.clip ? colours::text : colours::textFaint);
    g.setFont(uiFont(9.0f, true));
    g.drawText(values.outPeak > -100.0f ? juce::String(values.outPeak, 1).replace("-", juce::String::fromUTF8("\xe2\x88\x92")) : "",
               led.expanded(8.0f, 0.0f), juce::Justification::centred);
}
} // namespace subgen::ui
