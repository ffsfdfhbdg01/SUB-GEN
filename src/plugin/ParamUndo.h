// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace subgen::plugin
{
// Undo and redo of the editor's own parameter changes (SPEC_IO_UI §8): the editor records each gesture once it ends,
// so the host's automation never lands in the history. Changes of one parameter less than 500 ms apart make one step;
// at least 100 steps are kept. Message thread. Undo and redo set the parameter as one host gesture.
class ParamUndo
{
public:
    static constexpr double kCoalesceMs = 500.0;

    explicit ParamUndo(juce::AudioProcessorValueTreeState& parameters);

    // `paramId` moved from `before` to `after` (normalised 0..1); nowMs: the time of the change
    void recordChange(const juce::String& paramId, float before, float after,
                      double nowMs = juce::Time::getMillisecondCounterHiRes());
    bool undo();
    bool redo();
    bool canUndo() const { return manager.canUndo(); }
    bool canRedo() const { return manager.canRedo(); }
    juce::UndoManager& undoManager() { return manager; }

private:
    juce::AudioProcessorValueTreeState& state;
    juce::UndoManager manager{100 * 10, 100}; // an action is 10 units
    juce::String lastId;
    double lastMs = -1.0e9;
};
} // namespace subgen::plugin
