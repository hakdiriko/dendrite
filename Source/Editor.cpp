#include "Editor.h"

using namespace dendrite::ui;

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p), header (p), specimen (p), instruments (p, specimen), settings (p), trace (p), log (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    addAndMakeVisible (header);
    addAndMakeVisible (specimen);
    addAndMakeVisible (instruments);
    addAndMakeVisible (settings);
    addAndMakeVisible (trace);
    addAndMakeVisible (log);

    // any shape, up to a whole 8K screen; hosts' maximise and fullscreen work
    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, 7680, 4320);
    setSize (designWidth, designHeight);
    startTimerHz (30); // the specimen at 30 frames a second; the clock and the readouts at 10, a meter's rate
}

DendriteEditor::~DendriteEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DendriteEditor::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
}

void DendriteEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (Header::height));
    settings.setBounds (r.removeFromBottom (Settings::height));
    auto column = r.removeFromRight (Instruments::width);
    instruments.setBounds (column.removeFromTop (Instruments::height));
    log.setBounds (column);
    trace.setBounds (r.removeFromBottom (Trace::height));
    specimen.setBounds (r);
}

void DendriteEditor::timerCallback()
{
    animateTo (juce::Time::getMillisecondCounterHiRes() * 0.001);
}

void DendriteEditor::animateTo (double seconds)
{
    // the recording itself ticks in the processor; the window only draws what it holds
    specimen.tick (seconds);
    specimen.repaint();
    trace.repaint();
    if (frame++ % 3 == 0)
    {
        header.repaint();
        instruments.repaint();
        settings.repaint();
        log.repaint();
    }
}
