// The focus (3.6): one thing picked, and every part of the window shows it. A neuron type, a
// population, a reflex in the log or a word in the reference: the specimen lights its neurons and
// dims the rest, the trace draws their firing over the last bars, the log keeps only the lines
// about it, and the reference opens its entry. Picked by a click on a neuron, a region's name, a log
// line or a reflex in the trace, or by search ("/"). Nothing here touches the sound.
#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace dendrite::ui
{
struct Focus
{
    enum class Kind { none, type, population, event, term };

    Kind kind = Kind::none;
    juce::String key;               // the reference entry: a cell type's name, a term's key
    juce::String title;             // as the window names it
    std::vector<uint16_t> neurons;  // its neurons (none for a term that has none)
    juce::StringArray logWords;     // the log keeps the lines holding any of these
    double eventAt = -1.0;          // a picked log line: when it happened (session seconds)
    int cause = 0, move = 0;        // a picked reflex: its path
    int serial = 0;                 // counts every change

    bool active() const noexcept { return kind != Kind::none; }

    void set (Kind k, const juce::String& entry, const juce::String& name, std::vector<uint16_t> who, juce::StringArray words = {})
    {
        kind = k;
        key = entry;
        title = name;
        neurons = std::move (who);
        logWords = std::move (words);
        eventAt = -1.0;
        cause = move = 0;
        changed();
    }
    void clear()
    {
        if (kind == Kind::none)
            return;
        kind = Kind::none;
        key = title = {};
        neurons.clear();
        logWords.clear();
        eventAt = -1.0;
        changed();
    }
    void changed()
    {
        ++serial;
        for (auto& l : listeners)
            if (l)
                l();
    }
    std::vector<std::function<void()>> listeners;
};
} // namespace dendrite::ui
