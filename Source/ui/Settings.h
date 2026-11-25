// The settings line (3.0): the rig's switches, written out along the bottom edge as one line.
//   itch selector   how awake the fly is kept (click: its scale opens in the line itself;
//                   click or drag along it, the wheel steps; 3.4, the menu is gone)
//   heads           the delay line's rhythm: dotted, triplet or half, all three written out (click one)
//   dry             the dry path, on (0 dB) or off for a return track (click: switch)
//   output          live, or bypassed by the host; the specimen keeps recording either way
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Settings : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    explicit Settings (DendriteProcessor&);
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int height = 28;

private:
    enum Field { none = -1, itch, heads, dry, output, fields };
    Field fieldAt (juce::Point<float>) const;
    void set (const char* id, float value);
    void timerCallback() override;
    int itchAt (float x) const;  // the itch choice under x on the open scale, -1 if none
    int headsAt (juce::Point<float>) const;
    void close();

    static constexpr int itchChoices = 12;
    bool open = false;           // the itch scale is out
    bool scrubbing = false;      // a drag along the open scale
    int itchHover = -1;
    int headsHover = -1;
    std::array<float, itchChoices> itchX {};  // each choice's centre on the open scale
    float itchStep = 0.0f;
    float wheelSum = 0.0f;
    std::array<juce::Rectangle<float>, 3> headsAreas {};

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    std::array<juce::Rectangle<float>, fields> areas {};
    Field hover = none;
};
} // namespace dendrite::ui
