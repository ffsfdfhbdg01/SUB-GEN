// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugin/ParamUndo.h"

namespace subgen::plugin
{
namespace
{
void setAsGesture(juce::RangedAudioParameter* p, float value)
{
    if (p == nullptr)
        return;
    p->beginChangeGesture();
    p->setValueNotifyingHost(value);
    p->endChangeGesture();
}

class ParamChange : public juce::UndoableAction
{
public:
    ParamChange(juce::RangedAudioParameter* p, float b, float a) : param(p), before(b), after(a) {}

    // the first perform finds the value already set by the gesture
    bool perform() override
    {
        if (param != nullptr && param->getValue() != after)
            setAsGesture(param, after);
        return true;
    }

    bool undo() override
    {
        setAsGesture(param, before);
        return true;
    }

    juce::UndoableAction* createCoalescedAction(juce::UndoableAction* next) override
    {
        auto* n = dynamic_cast<ParamChange*>(next);
        if (n == nullptr || n->param != param)
            return nullptr;
        return new ParamChange(param, before, n->after);
    }

private:
    juce::RangedAudioParameter* param;
    float before, after;
};
} // namespace

ParamUndo::ParamUndo(juce::AudioProcessorValueTreeState& parameters) : state(parameters) {}

void ParamUndo::recordChange(const juce::String& paramId, float before, float after, double nowMs)
{
    auto* p = state.getParameter(paramId);
    if (p == nullptr || before == after)
        return;
    // a quick run of one parameter stays one transaction, so UndoManager coalesces it into one step
    if (paramId != lastId || nowMs - lastMs >= kCoalesceMs)
        manager.beginNewTransaction();
    lastId = paramId;
    lastMs = nowMs;
    manager.perform(new ParamChange(p, before, after));
}

bool ParamUndo::undo()
{
    lastId.clear();
    return manager.undo();
}

bool ParamUndo::redo()
{
    lastId.clear();
    return manager.redo();
}
} // namespace subgen::plugin
