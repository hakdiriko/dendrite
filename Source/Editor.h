#pragma once

#include "Processor.h"
#include "ui/Header.h"
#include "ui/Instruments.h"
#include "ui/Log.h"
#include "ui/Raster.h"
#include "ui/Rig.h"
#include "ui/Settings.h"
#include "ui/Specimen.h"

// dendrite's window (3.0): the display of a rig recording a fly's brain. The header names the
// specimen and keeps the clock; the instruments stand in a column on the right with the log under
// them, the raster runs under the specimen, the settings along the bottom, and the specimen takes
// the rest. Any size from 900 x 640 up; type keeps its
// size, the column keeps its width, and the specimen takes the room.
class DendriteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DendriteEditor (DendriteProcessor&);
    ~DendriteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 960, designHeight = 680;
    static constexpr int minWidth = 900, minHeight = 640;

    // For the snapshot tool: advance the window to a given time without a timer.
    void animateTo (double seconds);

private:
    void timerCallback() override;

    DendriteProcessor& processor;
    dendrite::ui::LookAndFeel lookAndFeel;
    dendrite::ui::Header header;
    dendrite::ui::Specimen specimen;
    dendrite::ui::Instruments instruments;
    dendrite::ui::Settings settings;
    dendrite::ui::Raster raster;
    dendrite::ui::Log log;
    juce::TooltipWindow tooltips { this, 900 };
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteEditor)
};
