#pragma once

#include "Processor.h"
#include "ui/Focus.h"
#include "ui/Header.h"
#include "ui/Reference.h"
#include "ui/Instruments.h"
#include "ui/LightsOut.h"
#include "ui/Log.h"
#include "ui/Rig.h"
#include "ui/Settings.h"
#include "ui/Specimen.h"
#include "ui/Trace.h"

// dendrite's window (3.0): the display of a rig recording a fly's brain. The header names the
// specimen and keeps the clock; the instruments stand in a column on the right with the log under
// them, the trace (what the fly does to your sound, 3.3) runs under the specimen, the settings along the bottom, and the specimen takes
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
    bool keyPressed (const juce::KeyPress&) override; // "/" searches, escape closes
    // the snapshot tool: open the reference on a term or a cell type, or search for some text
    void referenceForTools (const juce::String& what, bool searchOnly);
    // 3.7, lights out: only the brain and the light; the bench fades away (0.6 s) and comes back
    void setLightsOut (bool on);
    bool lightsAreOut() const noexcept { return lightsOut; }
    void mouseMove (const juce::MouseEvent&) override; // the hand at an edge brings the way back
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    DendriteProcessor& processor;
    dendrite::ui::LookAndFeel lookAndFeel;
    dendrite::ui::Focus focus; // 3.6: what is picked, shared by every part
    dendrite::ui::Header header;
    dendrite::ui::Specimen specimen;
    dendrite::ui::Instruments instruments;
    dendrite::ui::Settings settings;
    dendrite::ui::Trace trace;
    dendrite::ui::Log log;
    dendrite::ui::Reference reference;
    void placeReference();
    void layout();                 // the bench, or the brain alone, or between (dark)
    void showWayBack (juce::Point<int> hand);
    dendrite::ui::BenchStrip benchStrip;
    bool lightsOut = false;
    float dark = 0.0f;             // 0 the bench .. 1 lights out, moved at 1 / 0.6 s
    float darkShown = -1.0f;       // the eased value the window was last laid out at
    double darkClock = -1.0;
    juce::TooltipWindow tooltips { this, 900 };
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteEditor)
};
