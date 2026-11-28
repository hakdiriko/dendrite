// Lights out (3.7): the way back to the bench. With the lights out the window shows only the brain
// and the light; when the hand comes to any edge of the window, this one hairline comes back along
// the top, with the name on the left and "bench" on the right. A click anywhere on it (or escape,
// or "L") brings the bench back.
#pragma once

#include "Rig.h"

namespace dendrite::ui
{
class BenchStrip : public juce::Component
{
public:
    BenchStrip()
    {
        setInterceptsMouseClicks (true, false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    static constexpr int height = 30;
    std::function<void()> onBench;

    void paint (juce::Graphics& g) override
    {
        const Hairline line (g);
        g.setColour (juce::Colours::black.withAlpha (0.9f));
        g.fillAll();
        const auto font = fonts->at (type::header);
        g.setFont (font);
        const float h = (float) getHeight() - 1.0f;
        g.setColour (tone::grey (tone::bright));
        g.drawText ("dendrite", juce::Rectangle<float> (14.0f, 0.0f, 120.0f, h), juce::Justification::centredLeft);
        const juce::String back = juce::String (juce::CharPointer_UTF8 ("bench \xe2\x86\x93"));
        const float w = juce::GlyphArrangement::getStringWidth (font, back) + 16.0f;
        const auto box = juce::Rectangle<float> ((float) getWidth() - 14.0f - w, 5.0f, w, h - 10.0f);
        g.setColour (tone::grey (over ? tone::bright : tone::dim));
        g.drawRect (box, line.width());
        g.setColour (tone::grey (over ? tone::live : tone::mid));
        g.drawText (back, box, juce::Justification::centred);
        g.setColour (tone::grey (tone::dim));
        line.h (0.0f, (float) getWidth(), (float) getHeight() - 0.5f);
    }
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onBench && getLocalBounds().contains (e.getPosition()))
            onBench();
    }

private:
    bool over = false;
    juce::SharedResourcePointer<Fonts> fonts;
};
} // namespace dendrite::ui
