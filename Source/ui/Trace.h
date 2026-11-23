// The trace (3.3): what the fly does to your sound, under the specimen. Four rows on one time axis,
// the last 8 bars (or 16 s while the host is stopped): your sound (the input), the fly (its
// reflexes, grooms and steer), your echo (white where the fly bent the tape), and the tape's
// pitch in semitones. Each reflex draws one line down through the echo and the pitch: the cause
// above, what you hear below. Every mark comes from the timeline the processor keeps.
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Trace : public juce::Component, public juce::TooltipClient
{
public:
    explicit Trace (DendriteProcessor&);
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent& e) override { hoverAt = e.position; }
    void mouseExit (const juce::MouseEvent&) override { hoverAt = { -1.0f, -1.0f }; }

    static constexpr int height = 186; // the raster's height before it, so the specimen and its rail keep their room
    static constexpr int barsShown = 8;

private:
    struct Rows
    {
        juce::Rectangle<float> plot, sound, fly, echo, pitch;
    };
    Rows rows() const noexcept;
    double span() const noexcept; // seconds shown
    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::Point<float> hoverAt { -1.0f, -1.0f };
};
} // namespace dendrite::ui
