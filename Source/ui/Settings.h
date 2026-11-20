// The settings line (3.0): the rig's switches, written out along the bottom edge as one line.
//   itch selector   how awake the fly is kept (click: the choices)
//   heads           the delay line's rhythm: dotted, triplet or half (click: the next one)
//   dry             the dry path, on (0 dB) or off for a return track (click: switch)
//   output          live, or bypassed by the host; the specimen keeps recording either way
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Settings : public juce::Component, public juce::TooltipClient
{
public:
    explicit Settings (DendriteProcessor&);
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int height = 28;

private:
    enum Field { none = -1, itch, heads, dry, output, fields };
    Field fieldAt (juce::Point<float>) const;
    void set (const char* id, float value);

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    std::array<juce::Rectangle<float>, fields> areas {};
    Field hover = none;
};
} // namespace dendrite::ui
