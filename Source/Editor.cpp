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
    // 3.7, lights out: the header's word, "L", or the wheel pushed on past the smallest magnification
    header.onLightsOut = [this] { setLightsOut (true); };
    specimen.onStepBack = [this] { setLightsOut (true); };
    benchStrip.onBench = [this] { setLightsOut (false); };
    addChildComponent (benchStrip);
    addMouseListener (this, true);
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
    layout();
}

void DendriteEditor::layout()
{
    // 3.7: as the lights go out the bench fades and the specimen grows from its place to the
    // whole window (eased); out, the bench's parts are hidden, so nothing but the brain takes a hand
    const float t = juce::jlimit (0.0f, 1.0f, dark);
    const float e = t * t * (3.0f - 2.0f * t);
    darkShown = e;
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (Header::height));
    settings.setBounds (r.removeFromBottom (Settings::height));
    auto column = r.removeFromRight (Instruments::width);
    instruments.setBounds (column.removeFromTop (Instruments::height));
    log.setBounds (column);
    trace.setBounds (r.removeFromBottom (Trace::height));
    const auto all = getLocalBounds().toFloat(), bench = r.toFloat();
    auto lerp = [e] (float a, float b) { return a + (b - a) * e; };
    specimen.setBounds (juce::Rectangle<float> (lerp (bench.getX(), all.getX()), lerp (bench.getY(), all.getY()),
                                                lerp (bench.getWidth(), all.getWidth()), lerp (bench.getHeight(), all.getHeight()))
                            .toNearestInt());
    specimen.setBare (e);
    for (juce::Component* c : { (juce::Component*) &header, (juce::Component*) &settings, (juce::Component*) &instruments,
                                (juce::Component*) &log, (juce::Component*) &trace })
    {
        c->setAlpha (1.0f - e);
        c->setVisible (e < 0.999f);
    }
    benchStrip.setBounds (getLocalBounds().removeFromTop (BenchStrip::height));
    if (e < 0.999f)
        benchStrip.setVisible (false);
    if (e > 0.0f)
        reference.setVisible (false);
    else
        placeReference();
}

void DendriteEditor::setLightsOut (bool on)
{
    if (on == lightsOut)
        return;
    lightsOut = on;
    if (on && reference.isVisible())
        reference.close(); // the entry and the focus leave with the bench
    benchStrip.setVisible (false);
    if (isShowing())
        grabKeyboardFocus();
}

void DendriteEditor::showWayBack (juce::Point<int> hand)
{
    const int edge = 28;
    const bool near = hand.x < edge || hand.y < edge || hand.x > getWidth() - edge || hand.y > getHeight() - edge;
    benchStrip.setVisible (lightsOut && darkShown >= 0.999f && near);
}

void DendriteEditor::mouseMove (const juce::MouseEvent& e)
{
    if (lightsOut)
        showWayBack (e.getEventRelativeTo (this).getPosition());
}

void DendriteEditor::mouseExit (const juce::MouseEvent& e)
{
    // leaving the window across an edge counts as an edge; leaving a child inside it does not
    if (lightsOut && ! getLocalBounds().contains (e.getEventRelativeTo (this).getPosition()))
        benchStrip.setVisible (false);
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
    // 3.7: L puts the lights out and back on; escape brings the bench back first
    if ((k.getTextCharacter() == 'l' || k.getTextCharacter() == 'L') && ! k.getModifiers().isAnyModifierKeyDown())
    {
        setLightsOut (! lightsOut);
        return true;
    }
    if (k == juce::KeyPress::escapeKey && lightsOut)
    {
        setLightsOut (false);
        return true;
    }
    if (lightsOut && k.getTextCharacter() == '/')
        setLightsOut (false); // the search needs the bench: it comes back, then the field opens
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
    // 3.7: the lights go out (or come on) over 0.6 s
    {
        const double dt = darkClock < 0.0 ? 0.0 : juce::jlimit (0.0, 0.2, seconds - darkClock);
        darkClock = seconds;
        const float goal = lightsOut ? 1.0f : 0.0f;
        if (dark != goal)
            dark = goal > dark ? std::min (goal, dark + (float) dt / 0.6f) : std::max (goal, dark - (float) dt / 0.6f);
        const float t = juce::jlimit (0.0f, 1.0f, dark);
        if (std::abs (t * t * (3.0f - 2.0f * t) - darkShown) > 1.0e-4f)
            layout();
    }
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
