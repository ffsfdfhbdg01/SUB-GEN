// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/Editor.h"

#include <cmath>

#include "plugin/PluginProcessor.h"
#include "ui/Format.h"

namespace subgen::ui
{
namespace
{
using params::ParamId;
using plugin::Warning;

// the regions of the 1000 x 620 layout
constexpr int kTopBar = 40;
constexpr int kDisplayBottom = 250;
constexpr int kMeterWidth = 116;
constexpr int kDrawerTop = 584;
constexpr int kMainHeight = kDrawerTop - kDisplayBottom;
constexpr std::array<int, 5> kSectionX{0, 210, 480, 780, 1000}; // Source, Generator, Sub, Output

const char* engineBlurb(int engine)
{
    switch (engine)
    {
    case 0: return "Octave divider: a clean sub one or two octaves under the source.";
    case 1: return "Pitch tracker: a sine that follows the source's note.";
    case 2: return "Triggered voice: a pitched thump on each hit of the source.";
    case 3: return "Harmonics: overtones that let small speakers carry the bass.";
    case 4: return "Divide and harmonics together.";
    default: return "";
    }
}

// a control's name inside its group, without the words the group's title already says: "Band 2 Gain" in "Band 2" is
// "Gain", "Comp Threshold" in "Compressor" is "Threshold", "Front Delay" in "Route delay" is "Front", a group's own
// switch ("Gate", "Band 2 On") is "Active", a choice named as its group is "Mode"
juce::String shortLabel(const juce::String& name, const juce::String& title, bool isSwitch)
{
    auto words = juce::StringArray::fromTokens(name, " ", "");
    const auto titleWords = juce::StringArray::fromTokens(title, " ", "");
    if (words.size() >= 2 && words[0] == "Band" && title.startsWith("Band"))
        words.removeRange(0, 2);
    else if (!words.isEmpty() && titleWords[0].startsWithIgnoreCase(words[0]))
        words.remove(0);
    if (words.size() >= 2 && titleWords.size() >= 2 && words[words.size() - 1].equalsIgnoreCase(titleWords[titleWords.size() - 1]))
        words.remove(words.size() - 1);
    if (isSwitch && (words.isEmpty() || (words.size() == 1 && words[0] == "On")))
        return "Active";
    if (words.isEmpty()) // a choice named as its group: "Envelope" in "Envelope"
        return "Mode";
    return words.joinIntoString(" ");
}

float toDb(float linear) { return linear > 1e-6f ? 20.0f * std::log10(linear) : -120.0f; }

void drawTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    g.setColour(colours::textDim);
    g.setFont(uiFont(13.0f, true));
    g.drawText(text, area, juce::Justification::centredLeft);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------- panel

AdvancedPanel::AdvancedPanel(plugin::SubGenProcessor& p, int page, int engine) : pageIndex(page), engineIndex(engine)
{
    const auto& def = advancedPages()[static_cast<size_t>(page)];
    constexpr int cellW = 76, cellH = 88, gap = 30, top = 38;
    int x = 18;
    for (const auto& group : def.groups)
    {
        if (!groupVisible(group, engine))
            continue;
        Placed placed{group.title, {}, {}, false};
        const int n = static_cast<int>(group.ids.size()) + (group.ids.front() == ParamId::mix_law ? 1 : 0);
        const int cols = (n + 2) / 3;
        placed.area = {x, 10, cols * cellW, top - 10 + 3 * cellH};
        for (int i = 0; i < static_cast<int>(group.ids.size()); ++i)
        {
            const auto id = group.ids[static_cast<size_t>(i)];
            auto c = makeControl(p.parameters(), &p.paramUndo(), id, Knob::Size::Small);
            auto cell = juce::Rectangle<int>(x + (i % cols) * cellW, top + (i / cols) * cellH, cellW, cellH);
            c->setLabel(shortLabel(c->label(), group.title, params::spec(id).type == params::ParamType::Bool));
            if (params::spec(id).type == params::ParamType::Float)
                c->setBounds(cell);
            else
                c->setBounds(cell.reduced(4, 0).withHeight(46).withY(cell.getY() + 14));
            if (i == 0 && params::spec(id).type == params::ParamType::Bool && juce::String(params::spec(id).id).endsWith("_on"))
                placed.firstIsSwitch = true;
            addAndMakeVisible(*c);
            placed.controls.push_back(c.get());
            controls.push_back(std::move(c));
        }
        if (group.ids.front() == ParamId::mix_law) // the dry delay's Auto action (SPEC §9.2)
        {
            autoDelay = std::make_unique<FlatButton>("Auto delay");
            autoDelay->setTooltip("Set Align to the sub path's delay at the current pitch");
            const int i = static_cast<int>(group.ids.size());
            autoDelay->setBounds(x + (i % cols) * cellW + 4, top + (i / cols) * cellH + 30, cellW - 8, 26);
            autoDelay->onClick = [&p] { p.applyAutoAlignMs(); };
            addAndMakeVisible(*autoDelay);
        }
        groups.push_back(std::move(placed));
        x += cols * cellW + gap;
    }
    updateDimming();
    setTitle(juce::String(def.name) + " settings");
}

void AdvancedPanel::updateDimming()
{
    for (auto& g : groups)
        if (g.firstIsSwitch)
        {
            const bool on = g.controls.front()->binding().normalised() >= 0.5f;
            for (size_t i = 1; i < g.controls.size(); ++i)
                g.controls[i]->setDimmed(!on);
        }
}

void AdvancedPanel::paint(juce::Graphics& g)
{
    g.fillAll(colours::panel);
    g.setColour(colours::line);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    for (const auto& group : groups)
    {
        g.setColour(colours::textDim);
        g.setFont(uiFont(12.0f, true));
        g.drawText(group.title, group.area.withHeight(18).withTrimmedLeft(4), juce::Justification::centredLeft);
        g.setColour(colours::line.withAlpha(0.7f));
        g.drawHorizontalLine(group.area.getY() + 22, static_cast<float>(group.area.getX() + 4),
                             static_cast<float>(group.area.getRight() - 4));
    }
    if (pageIndex == kEqPage)
    {
        g.setColour(colours::textFaint);
        g.setFont(uiFont(11.5f));
        g.drawText("Drag a band's node on the graph; the wheel sets its Q, a double-click turns it on or off.",
                   getLocalBounds().removeFromBottom(22).withTrimmedLeft(22), juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------------------------------------------------- top bar bits

PowerSwitch::PowerSwitch(juce::AudioProcessorValueTreeState& state, plugin::ParamUndo* undo)
    : ParamControl(state, undo, ParamId::bypass)
{
    setLabel("Bypass");
}

void PowerSwitch::paint(juce::Graphics& g)
{
    const bool active = bind.normalised() < 0.5f; // not bypassed
    const auto b = getLocalBounds().toFloat().reduced(3.0f);
    const auto c = b.getCentre();
    const float r = std::min(b.getWidth(), b.getHeight()) / 2.0f - 3.0f;
    const auto colour = active ? colours::sub : colours::textFaint;
    if (active)
    {
        g.setColour(colours::sub.withAlpha(0.14f));
        g.fillEllipse(b);
    }
    juce::Path ring;
    ring.addCentredArc(c.x, c.y, r * 0.62f, r * 0.62f, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
    g.setColour(colour);
    g.strokePath(ring, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine(c.x, c.y - r * 0.82f, c.x, c.y - r * 0.15f, 1.8f);
}

void PowerSwitch::mouseDown(const juce::MouseEvent&) { bind.setPlainAsGesture(bind.normalised() >= 0.5f ? 0.0f : 1.0f); }

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced(4.0f);
    if (highlighted || down)
    {
        g.setColour(colours::raised.brighter(down ? 0.3f : 0.1f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
    }
    const auto c = b.getCentre();
    const float r = b.getWidth() * 0.32f;
    const bool undo = icon == Icon::Undo;
    juce::Path arc;
    arc.addCentredArc(c.x, c.y + 1.0f, r, r, 0.0f, undo ? -1.9f : 1.9f, undo ? 1.3f : -1.3f, true);
    g.setColour((isEnabled() ? colours::text : colours::textFaint));
    g.strokePath(arc, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto tip = juce::Point<float>(c.x + (undo ? -1.0f : 1.0f) * r * std::sin(1.9f), c.y + 1.0f - r * std::cos(1.9f));
    juce::Path head;
    head.addTriangle(tip.x + (undo ? -3.0f : 3.0f), tip.y - 3.5f, tip.x + (undo ? 3.5f : -3.5f), tip.y - 1.0f,
                     tip.x + (undo ? -1.0f : 1.0f), tip.y + 3.5f);
    g.fillPath(head);
}

// ---------------------------------------------------------------------------------------------------------- editor

SubGenEditor::SubGenEditor(plugin::SubGenProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), display(p.parameters(), &p.paramUndo())
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(content);
    auto& state = p.parameters();
    auto* undo = &p.paramUndo();

    // top bar
    content.addAndMakeVisible(undoButton);
    content.addAndMakeVisible(redoButton);
    undoButton.setTooltip("Undo (Cmd/Ctrl+Z)");
    redoButton.setTooltip("Redo (Shift+Cmd/Ctrl+Z)");
    undoButton.onClick = [this] { proc.paramUndo().undo(); updateTopBar(); };
    redoButton.onClick = [this] { proc.paramUndo().redo(); updateTopBar(); };
    quality = std::make_unique<Segmented>(state, undo, ParamId::quality, false);
    quality->setTooltip("Quality: Live (least latency), Mix, Print (oversampling, linear phase)");
    content.addAndMakeVisible(*quality);
    power = std::make_unique<PowerSwitch>(state, undo);
    content.addAndMakeVisible(*power);
    scaleButton.onClick = [this] { showScaleMenu(); };
    scaleButton.setTooltip("Interface size");
    content.addAndMakeVisible(scaleButton);

    // analyser and meters
    content.addAndMakeVisible(display);
    content.addAndMakeVisible(meters);
    meters.onClipReset = [this] { clipUntil = 0.0; };
    sampleRate = p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0;
    for (auto& s : spectra)
        s.prepare(sampleRate);
    for (auto& levels : shown)
        levels.fill(Spectrum::kFloorDb);
    for (auto& lane : lanes)
        lane.assign(8192, 0.0f);
    display.setSampleRate(sampleRate);

    // main sections
    for (const auto& section : mainSections())
        for (auto id : section.ids)
        {
            std::unique_ptr<ParamControl> c;
            const auto& s = params::spec(id);
            if (id == ParamId::drive)
                c = std::make_unique<Knob>(state, undo, id, Knob::Size::Hero);
            else if (id == ParamId::engine || id == ParamId::sum_mode)
                c = std::make_unique<Segmented>(state, undo, id, false);
            else if (s.type == params::ParamType::Float)
                c = std::make_unique<Knob>(state, undo, id, Knob::Size::Medium);
            else if (s.type == params::ParamType::Bool)
                c = std::make_unique<Toggle>(state, undo, id, false);
            else
                c = std::make_unique<ChoiceBox>(state, undo, id, true);
            content.addAndMakeVisible(*c);
            mainControls.push_back(std::move(c));
        }
    control(ParamId::range_lo)->setLabel("Range low");
    control(ParamId::range_hi)->setLabel("Range high");
    control(ParamId::det_thresh)->setLabel("Threshold");
    control(ParamId::src_mode)->setLabel("Source");
    control(ParamId::sub1_level)->setLabel(juce::String::fromUTF8("Sub \xe2\x88\x92" "1"));
    control(ParamId::sub2_level)->setLabel(juce::String::fromUTF8("Sub \xe2\x88\x92" "2"));
    control(ParamId::drive_mode)->setLabel("Character");
    control(ParamId::sub_level)->setLabel("Sub level");
    control(ParamId::boost_gain)->setLabel("Boost");
    control(ParamId::sub_pol)->setLabel("Invert polarity");
    control(ParamId::dry_level)->setLabel("Dry");
    control(ParamId::out_trim)->setLabel("Output");
    control(ParamId::wet_only)->setLabel("Wet only");
    control(ParamId::out_mode)->setLabel("Output mode");
    autoAlign.accent = true;
    autoAlign.setTooltip("Find the sub's polarity and phase rotation that sum best with the dry (plays a few seconds first)");
    autoAlign.onClick = [this] { startAutoAlign(); };
    content.addAndMakeVisible(autoAlign);

    // the Advanced drawer
    for (int i = 0; i < static_cast<int>(advancedPages().size()); ++i)
    {
        auto tab = std::make_unique<FlatButton>(advancedPages()[static_cast<size_t>(i)].name);
        tab->setClickingTogglesState(false);
        tab->onClick = [this, i] { showAdvancedPage(openPage == i ? -1 : i); };
        content.addAndMakeVisible(*tab);
        tabs.push_back(std::move(tab));
    }
    layoutMain();

    const auto extra = p.extraState();
    const double startScale = juce::jlimit(kMinScale, kMaxScale, extra.uiScale);
    setSize(juce::roundToInt(kEditorWidth * startScale), juce::roundToInt(kEditorHeight * startScale));
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(kEditorWidth * kMinScale), juce::roundToInt(kEditorHeight * kMinScale),
                    juce::roundToInt(kEditorWidth * kMaxScale), juce::roundToInt(kEditorHeight * kMaxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(static_cast<double>(kEditorWidth) / kEditorHeight);
    setUiScale(startScale);
    setWantsKeyboardFocus(true);

    proc.uiFeed().setEditorOpen(true);
    updateContext();
    updateTopBar();
    startTimerHz(30);
}

SubGenEditor::~SubGenEditor()
{
    stopTimer();
    proc.uiFeed().setEditorOpen(false);
    setLookAndFeel(nullptr);
}

ParamControl* SubGenEditor::control(ParamId id)
{
    for (auto& c : mainControls)
        if (c->binding().id() == id)
            return c.get();
    return nullptr;
}

void SubGenEditor::layoutMain()
{
    content.setBounds(0, 0, kEditorWidth, kEditorHeight);
    undoButton.setBounds(132, 7, 26, 26);
    redoButton.setBounds(160, 7, 26, 26);
    quality->setBounds(668, 7, 174, 26);
    scaleButton.setBounds(852, 7, 62, 26);
    power->setBounds(956, 4, 32, 32);
    display.setBounds(0, kTopBar, kEditorWidth - kMeterWidth, kDisplayBottom - kTopBar);
    meters.setBounds(kEditorWidth - kMeterWidth, kTopBar, kMeterWidth, kDisplayBottom - kTopBar);

    const int y0 = kDisplayBottom + 34; // under the section titles
    const auto med = Knob::cellSize(Knob::Size::Medium);
    // Source
    control(ParamId::range_lo)->setBounds(12, y0, med.x, med.y);
    control(ParamId::range_hi)->setBounds(106, y0, med.x, med.y);
    control(ParamId::det_thresh)->setBounds(12, y0 + 128, med.x, med.y);
    control(ParamId::src_mode)->setBounds(110, y0 + 132, 86, 46);
    // Generator
    control(ParamId::engine)->setBounds(226, y0 - 2, 238, 28);
    control(ParamId::sub1_level)->setBounds(222, y0 + 46, med.x, med.y);
    control(ParamId::sub2_level)->setBounds(299, y0 + 46, med.x, med.y);
    control(ParamId::shape)->setBounds(376, y0 + 46, med.x, med.y);
    // Sub
    const auto hero = Knob::cellSize(Knob::Size::Hero);
    control(ParamId::drive)->setBounds(490, y0 - 8, hero.x, hero.y);
    control(ParamId::drive_mode)->setBounds(674, y0, 96, 46);
    control(ParamId::sub_level)->setBounds(676, y0 + 54, med.x, med.y);
    control(ParamId::boost_gain)->setBounds(676, y0 + 164, med.x, med.y);
    control(ParamId::sub_pol)->setBounds(492, y0 + 210, 110, 28);
    autoAlign.setBounds(608, y0 + 210, 78, 28);
    // Output
    control(ParamId::sum_mode)->setBounds(796, y0 - 2, 188, 28);
    control(ParamId::dry_level)->setBounds(796, y0 + 40, med.x, med.y);
    control(ParamId::mix)->setBounds(796, y0 + 40, med.x, med.y);
    control(ParamId::out_trim)->setBounds(892, y0 + 40, med.x, med.y);
    control(ParamId::wet_only)->setBounds(800, y0 + 158, 180, 28);
    control(ParamId::out_mode)->setBounds(800, y0 + 196, 180, 46);

    const int tabW = 84;
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i]->setBounds(112 + static_cast<int>(i) * (tabW + 6), kDrawerTop + 5, tabW, 26);
}

void SubGenEditor::resized()
{
    scale = static_cast<double>(getWidth()) / kEditorWidth;
    content.setTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
    auto extra = proc.extraState();
    if (std::abs(extra.uiScale - scale) > 1e-6)
    {
        extra.uiScale = scale;
        proc.setExtraState(extra);
    }
    scaleButton.setButtonText(juce::String(juce::roundToInt(scale * 100.0)) + " %");
}

void SubGenEditor::setUiScale(double s)
{
    const double clamped = juce::jlimit(kMinScale, kMaxScale, s);
    setSize(juce::roundToInt(kEditorWidth * clamped), juce::roundToInt(kEditorHeight * clamped));
}

void SubGenEditor::showScaleMenu()
{
    juce::PopupMenu menu;
    const std::array<int, 6> sizes{75, 100, 125, 150, 175, 200};
    for (int s : sizes)
        menu.addItem(s, juce::String(s) + " %", true, juce::roundToInt(scale * 100.0) == s);
    juce::Component::SafePointer<SubGenEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&scaleButton), [safe](int r) {
        if (safe != nullptr && r > 0)
            safe->setUiScale(r / 100.0);
    });
}

void SubGenEditor::showAdvancedPage(int page, bool animate)
{
    if (page < -1 || page >= static_cast<int>(advancedPages().size()))
        page = -1;
    openPage = page;
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        tabs[i]->setToggleState(static_cast<int>(i) == page, juce::dontSendNotification);
        tabs[i]->accent = static_cast<int>(i) == page;
        tabs[i]->repaint();
    }
    display.setEqVisible(page == kEqPage);
    if (page < 0)
    {
        panel.reset();
        content.repaint();
        return;
    }
    const int engine = static_cast<int>(std::lround(proc.parameters().getRawParameterValue("engine")->load()));
    panel = std::make_unique<AdvancedPanel>(proc, page, engine);
    content.addAndMakeVisible(*panel);
    const juce::Rectangle<int> target(0, kDisplayBottom, kEditorWidth, kMainHeight);
    auto& animator = juce::Desktop::getInstance().getAnimator();
    if (animate)
    {
        panel->setBounds(target.withY(kDrawerTop - 40));
        panel->setAlpha(0.0f);
        animator.animateComponent(panel.get(), target, 1.0f, 170, false, 0.2, 0.0);
    }
    else
        panel->setBounds(target);
    for (auto& t : tabs) // the drawer's tabs stay above the panel
        t->toFront(false);
    content.repaint();
}

bool SubGenEditor::keyPressed(const juce::KeyPress& key)
{
    const bool z = key.getKeyCode() == 'Z' || key.getKeyCode() == 'z';
    if (z && key.getModifiers().isCommandDown())
    {
        if (key.getModifiers().isShiftDown())
            proc.paramUndo().redo();
        else
            proc.paramUndo().undo();
        updateTopBar();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::escapeKey) && openPage >= 0)
    {
        showAdvancedPage(-1);
        return true;
    }
    return false;
}

void SubGenEditor::startAutoAlign()
{
    alignCountAtRequest = proc.autoAlignCount();
    alignStatus = juce::String::fromUTF8("Analysing\xe2\x80\xa6");
    proc.requestAutoAlign();
    content.repaint();
}

juce::String SubGenEditor::warningText(juce::String& all) const
{
    // SPEC_IO_UI §7.4, the most pressing first; W-06 is the meters' clip light
    static const std::array<std::pair<Warning, const char*>, 10> texts{{
        {Warning::LatencyChanged, "Latency changed \xe2\x80\x94 restart playback"},
        {Warning::LayoutNotSupported, "Layout not supported"},
        {Warning::NoLfeChannel, "No LFE channel in this layout"},
        {Warning::KeyNotConnected, "External key not connected"},
        {Warning::LinearPhaseNeedsPrint, "Linear phase requires Print"},
        {Warning::SourceOutOfPhase, "Source is out of phase \xe2\x80\x94 use Auto"},
        {Warning::SubCancelsInMono, "Sub L/R cancel in mono"},
        {Warning::LfeOnlyContent, "Content only in LFE is lost in stereo downmix"},
        {Warning::LowConfidence, "Low confidence \xe2\x80\x94 sub gated"},
        {Warning::RangeCorrected, "Range high kept at 1.2 \xc3\x97 range low"},
    }};
    juce::String first;
    juce::StringArray lines;
    for (const auto& [w, text] : texts)
        if (proc.warnings().has(w))
        {
            lines.add(juce::String::fromUTF8(text));
            if (first.isEmpty())
                first = juce::String::fromUTF8(text);
        }
    all = lines.joinIntoString("\n");
    return first;
}

void SubGenEditor::updateTopBar()
{
    undoButton.setEnabled(proc.paramUndo().canUndo());
    redoButton.setEnabled(proc.paramUndo().canRedo());
    juce::String all;
    const auto w = warningText(all);
    if (w != warning || all != warningAll)
    {
        warning = w;
        warningAll = all;
        content.repaint(0, 0, kEditorWidth, kTopBar);
    }
}

void SubGenEditor::updateContext()
{
    auto value = [this](const char* id) { return proc.parameters().getRawParameterValue(id)->load(); };
    const bool mixMode = value("sum_mode") >= 0.5f;
    control(ParamId::dry_level)->setVisible(!mixMode);
    control(ParamId::mix)->setVisible(mixMode);
    const bool wet = value("wet_only") >= 0.5f;
    control(ParamId::dry_level)->setDimmed(wet);
    control(ParamId::mix)->setDimmed(wet);
    const int engine = static_cast<int>(std::lround(value("engine")));
    control(ParamId::sub2_level)->setDimmed(engine == 3); // HARMONIC has no Sub −2
    control(ParamId::sub1_level)->setLabel(engine == 3 ? juce::String("Harmonics")
                                                       : juce::String::fromUTF8("Sub \xe2\x88\x92" "1"));
    const auto blurb = juce::String(engineBlurb(engine));
    if (blurb != engineText)
    {
        engineText = blurb;
        content.repaint(kSectionX[1], kDisplayBottom, kSectionX[2] - kSectionX[1], kMainHeight);
    }
    if (panel != nullptr)
    {
        if (panel->page() == 1 && panel->engine() != engine) // the engine page follows the engine
            showAdvancedPage(1, false);
        else
            panel->updateDimming();
    }
    if (alignCountAtRequest >= 0 && proc.autoAlignCount() != alignCountAtRequest)
    {
        const auto r = proc.lastAutoAlign();
        alignCountAtRequest = -1;
        if (r.clearOptimum)
            alignStatus = juce::String("Aligned: ") + (r.polarity < 0 ? juce::String::fromUTF8("180\xc2\xb0") : juce::String::fromUTF8("0\xc2\xb0"))
                        + ", " + formatValue(ParamId::sub_rot, r.rotationDeg) + " rotation";
        else
            alignStatus = "No clear optimum: left as it was";
        content.repaint();
    }
}

void SubGenEditor::readFeed()
{
    auto& feed = proc.uiFeed();
    std::array<float*, plugin::UiFeed::kLanes> dst{};
    for (size_t l = 0; l < lanes.size(); ++l)
        dst[l] = lanes[l].data();
    float inPeak = 0.0f, subPeak = 0.0f, outPeak = 0.0f;
    bool received = false;
    for (int got = feed.read(dst, 8192); got > 0; got = feed.read(dst, 8192))
    {
        received = true;
        spectra[0].push(lanes[0].data(), got);
        spectra[1].push(lanes[1].data(), got);
        spectra[2].push(lanes[2].data(), got);
        double si = 0.0, ss = 0.0, so = 0.0;
        for (int i = 0; i < got; ++i)
        {
            const float d = lanes[0][static_cast<size_t>(i)], s = lanes[1][static_cast<size_t>(i)],
                        o = lanes[3][static_cast<size_t>(i)];
            inPeak = std::max(inPeak, std::abs(d));
            subPeak = std::max(subPeak, std::abs(s));
            outPeak = std::max(outPeak, o);
            si += d * d;
            ss += s * s;
            so += o * o;
        }
        // RMS over 300 ms: one exponential step per chunk
        const float k = 1.0f - std::exp(-static_cast<float>(got / sampleRate) / 0.3f);
        inMs += k * (static_cast<float>(si / got) - inMs);
        subMs += k * (static_cast<float>(ss / got) - subMs);
        outMs += k * (static_cast<float>(so / got) - outMs);
    }
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastTick > 0.0 ? static_cast<float>(std::min(0.2, (now - lastTick) / 1000.0)) : 1.0f / 30.0f;
    lastTick = now;
    const float fall = 26.0f * dt; // dB the peaks fall per tick
    auto peakOf = [fall](float held, float peak) { return std::max(toDb(peak), held - fall); };
    meterValues.inPeak = peakOf(meterValues.inPeak, inPeak);
    meterValues.subPeak = peakOf(meterValues.subPeak, subPeak);
    meterValues.outPeak = peakOf(meterValues.outPeak, outPeak);
    meterValues.inRms = toDb(std::sqrt(inMs));
    meterValues.subRms = toDb(std::sqrt(subMs));
    meterValues.outRms = toDb(std::sqrt(outMs));
    if (outPeak >= 1.0f || proc.warnings().has(Warning::Clip))
        clipUntil = now + 3000.0;
    meterValues.clip = now < clipUntil;
    auto value = [this](const char* id) { return proc.parameters().getRawParameterValue(id)->load(); };
    meterValues.compOn = value("comp_on") >= 0.5f;
    meterValues.limOn = value("lim_on") >= 0.5f;
    meterValues.compGr = feed.compGainReductionDb();
    meterValues.limGr = std::min(60.0f, feed.limiterGainReductionDb());
    meters.setValues(meterValues);

    // the spectra: up at once, down at 30 dB/s; with no audio coming (stopped, bypassed) they sink to the floor
    for (size_t s = 0; s < spectra.size(); ++s)
    {
        spectra[s].consumeUpdate();
        const auto& in = spectra[s].levelsDb();
        for (size_t i = 0; i < shown[s].size(); ++i)
        {
            const float target = received ? in[i] : Spectrum::kFloorDb;
            shown[s][i] = std::max(target, shown[s][i] - 30.0f * dt);
        }
    }
    display.setLevels(shown[0], shown[1], shown[2]);
    display.setPitch(proc.subFrequencyHz(), meterValues.subRms > -70.0f);
}

void SubGenEditor::timerCallback()
{
    readFeed();
    updateContext();
    updateTopBar();
}

void SubGenEditor::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void SubGenEditor::Content::paint(juce::Graphics& g)
{
    auto& e = editor;
    g.fillAll(colours::background);

    // the top bar: the name, the preset, a warning
    auto bar = juce::Rectangle<int>(0, 0, kEditorWidth, kTopBar);
    g.setColour(colours::panel);
    g.fillRect(bar);
    {
        // the wordmark: a half wave dropping an octave under the name
        juce::Path wave;
        const float x0 = 16.0f, y0 = 20.0f;
        wave.startNewSubPath(x0, y0);
        for (int i = 1; i <= 24; ++i)
        {
            const float t = static_cast<float>(i) / 24.0f;
            wave.lineTo(x0 + 18.0f * t, y0 - 6.0f * std::sin(juce::MathConstants<float>::pi * t));
        }
        g.setColour(colours::sub);
        g.strokePath(wave, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colours::sub.withAlpha(0.5f));
        g.drawLine(x0, y0 + 4.0f, x0 + 18.0f, y0 + 4.0f, 1.2f);
        g.setColour(colours::text);
        g.setFont(uiFont(16.0f, true));
        g.drawText("SUB-GEN", juce::Rectangle<int>(42, 0, 84, kTopBar), juce::Justification::centredLeft);
    }
    const auto extra = e.proc.extraState();
    g.setFont(uiFont(13.0f));
    g.setColour(colours::text);
    g.drawText(extra.presetName, juce::Rectangle<int>(204, 0, 150, kTopBar), juce::Justification::centredLeft);
    g.setColour(colours::textFaint);
    g.setFont(uiFont(11.5f));
    const float presetW = juce::GlyphArrangement::getStringWidth(uiFont(13.0f), extra.presetName);
    g.drawText(extra.presetCategory, juce::Rectangle<int>(204 + juce::roundToInt(presetW) + 8, 0, 120, kTopBar),
               juce::Justification::centredLeft);
    if (e.warning.isNotEmpty())
    {
        const auto area = juce::Rectangle<float>(372.0f, 9.0f, 284.0f, 22.0f);
        g.setColour(colours::warning.withAlpha(0.12f));
        g.fillRoundedRectangle(area, 11.0f);
        g.setColour(colours::warning);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({area.getX() + 12.0f, area.getCentreY()}));
        g.setFont(uiFont(11.5f));
        g.drawFittedText(e.warning, area.withTrimmedLeft(22.0f).withTrimmedRight(8.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.8f);
    }
    g.setColour(colours::line);
    g.drawHorizontalLine(kTopBar - 1, 0.0f, static_cast<float>(kEditorWidth));

    // the sections: hairlines between them, a title each
    const auto main = juce::Rectangle<int>(0, kDisplayBottom, kEditorWidth, kMainHeight);
    g.setColour(colours::panel);
    g.fillRect(main);
    g.setColour(colours::line);
    g.drawHorizontalLine(kDisplayBottom, 0.0f, static_cast<float>(kEditorWidth));
    const auto& sections = mainSections();
    for (size_t i = 0; i < sections.size(); ++i)
    {
        if (i > 0)
            g.drawVerticalLine(kSectionX[i], static_cast<float>(kDisplayBottom + 12), static_cast<float>(kDrawerTop - 12));
        drawTitle(g, juce::Rectangle<int>(kSectionX[i] + 18, kDisplayBottom + 8, 160, 20), sections[i].title);
        g.setColour(colours::line);
    }
    // the generator's engine in a sentence
    g.setColour(colours::textFaint);
    g.setFont(uiFont(11.5f));
    g.drawFittedText(e.engineText, juce::Rectangle<int>(228, kDisplayBottom + 206, 236, 34), juce::Justification::topLeft, 2, 1.0f);
    // the Auto-align result
    g.setColour(colours::textDim);
    g.drawFittedText(e.alignStatus, juce::Rectangle<int>(496, kDrawerTop - 46, 270, 18), juce::Justification::centredLeft, 1, 0.85f);

    // the drawer
    const auto drawer = juce::Rectangle<int>(0, kDrawerTop, kEditorWidth, kEditorHeight - kDrawerTop);
    g.setColour(colours::background);
    g.fillRect(drawer);
    g.setColour(colours::line);
    g.drawHorizontalLine(kDrawerTop, 0.0f, static_cast<float>(kEditorWidth));
    g.setColour(colours::textDim);
    g.setFont(uiFont(12.5f, true));
    g.drawText("Advanced", drawer.withX(18).withWidth(90), juce::Justification::centredLeft);
    g.setColour(colours::textFaint);
    g.setFont(uiFont(11.0f));
    g.drawText(e.openPage >= 0 ? "Esc closes" : "", drawer.withTrimmedRight(18), juce::Justification::centredRight);
}
} // namespace subgen::ui
