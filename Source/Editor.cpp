#include "Editor.h"

using namespace dendrite::ui;

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p), header (p), specimen (p), instruments (p, specimen), settings (p), trace (p), log (p), reference (p, focus)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    addAndMakeVisible (header);
    addAndMakeVisible (specimen);
    addAndMakeVisible (instruments);
    addAndMakeVisible (settings);
    addAndMakeVisible (trace);
    addAndMakeVisible (log);
    // one story (3.4): the hand on a reflex in the trace lights the path that made it
    trace.onReflexHover = [this] (int cause, int move) { specimen.lightPath (cause, move); };
    // 3.6: one focus for every part; the reference opens over the specimen's left side
    addChildComponent (reference);
    specimen.setFocus (&focus);
    trace.setFocus (&focus);
    log.setFocus (&focus);
    header.onSearch = [this] { reference.openSearch(); };
    reference.onLayout = [this] { placeReference(); };
    specimen.onPickType = [this] (int t) { reference.pickType (t); };
    specimen.onPickTerm = [this] (const juce::String& k) { reference.pickTerm (k); };
    focus.listeners.push_back ([this] {
        placeReference();
        if (focus.kind == Focus::Kind::event && focus.cause > 0)
            specimen.lightPath (focus.cause, focus.move);
        processor.recording.watch (focus.neurons);
    });
    setWantsKeyboardFocus (true);

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
    placeReference();
}

void DendriteEditor::placeReference()
{
    const auto s = specimen.getBounds();
    reference.setBounds (s.withWidth (std::min (Reference::width, s.getWidth() / 2)));
    reference.setVisible (reference.wanted());
    specimen.setReferenceWidth (reference.isVisible() ? reference.getWidth() : 0);
}

bool DendriteEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getTextCharacter() == '/' || (k.getModifiers().isCommandDown() && k.getKeyCode() == 'F'))
    {
        reference.openSearch();
        placeReference();
        return true;
    }
    if (k == juce::KeyPress::escapeKey && reference.isVisible())
    {
        reference.close();
        placeReference();
        return true;
    }
    return false;
}

void DendriteEditor::referenceForTools (const juce::String& what, bool searchOnly)
{
    if (searchOnly)
        reference.openSearch (what);
    else if (dendrite::ui::glossary::term (what) != nullptr)
        reference.pickTerm (what);
    else
        reference.pickType (dendrite::ui::glossary::typeIndex (what));
    placeReference();
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
        reference.tick();
        header.repaint();
        instruments.repaint();
        settings.repaint();
        log.repaint();
    }
}
