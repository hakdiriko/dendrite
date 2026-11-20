// The log (3.0): what happened, written down as it happened, newest at the bottom. Lines that keep
// still holds on tape are handles: pull one into Live to get that take, bar to bar.
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Log : public juce::Component, public juce::TooltipClient
{
public:
    explicit Log (DendriteProcessor&);
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Line
    {
        float y;
        uint32_t mark;
        juce::String text;
    };
    const Line* lineAt (juce::Point<float>) const;
    bool held (uint32_t mark) const; // keep still has it on tape

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    std::vector<Line> lines;
    juce::Point<float> mouseAt { -1.0f, -1.0f };
    bool pressed = false, dragged = false, shift = false;
    uint32_t pressedMark = 0;
};
} // namespace dendrite::ui
