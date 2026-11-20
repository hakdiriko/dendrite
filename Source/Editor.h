#pragma once

#include "Processor.h"
#include "ui/Header.h"
#include "ui/Rig.h"
#include "ui/Specimen.h"

// dendrite's window (3.0): the display of a rig recording a fly's brain. The header names the
// specimen and keeps the clock; the specimen fills the rest. Any size from 720 x 460 up; type keeps
// its size and the specimen takes the room.
class DendriteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DendriteEditor (DendriteProcessor&);
    ~DendriteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 880, designHeight = 560;
    static constexpr int minWidth = 720, minHeight = 460;

    // For the snapshot tool: advance the window to a given time without a timer.
    void animateTo (double seconds);

private:
    void timerCallback() override;

    DendriteProcessor& processor;
    dendrite::ui::LookAndFeel lookAndFeel;
    dendrite::ui::Header header;
    dendrite::ui::Specimen specimen;
    juce::TooltipWindow tooltips { this, 900 };
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteEditor)
};
