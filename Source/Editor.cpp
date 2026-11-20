#include "Editor.h"

using namespace dendrite::ui;

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p), header (p), specimen (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    addAndMakeVisible (header);
    addAndMakeVisible (specimen);

    // any shape, up to a whole 8K screen; hosts' maximise and fullscreen work
    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, 7680, 4320);
    setSize (designWidth, designHeight);
    startTimerHz (30); // the specimen at 30 frames a second, the header's clock at 10
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
    specimen.setBounds (r);
}

void DendriteEditor::timerCallback()
{
    animateTo (juce::Time::getMillisecondCounterHiRes() * 0.001);
}

void DendriteEditor::animateTo (double)
{
    // the recording itself ticks in the processor; the window only draws what it holds
    specimen.repaint();
    if (frame++ % 3 == 0)
        header.repaint();
}
