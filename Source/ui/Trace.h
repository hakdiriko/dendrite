// The trace (3.3): what the fly does to your sound, under the specimen. Four rows on one time axis,
// the last 8 bars (or 16 s while the host is stopped): your sound (the input), the fly (its
// reflexes, grooms and steer), your echo (white where the fly bent the tape), and the tape's
// pitch in semitones. Since 3.4 one more bar after now, dashed: the move the fly has decided and the
// grid has not played yet. Each reflex draws one line down through the echo and the pitch: the cause
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
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override { hoverAt = { -1.0f, -1.0f }; }

    static constexpr int height = 186; // the raster's height before it, so the specimen and its rail keep their room
    static constexpr int barsShown = 8;

    // the hand rests on a reflex's line: its cause (Timeline::Reflex::cause) and move, so the
    // specimen can light the path that made it (3.4)
    std::function<void (int cause, int move)> onReflexHover;

private:
    struct Rows
    {
        juce::Rectangle<float> plot, sound, fly, echo, pitch;
    };
    Rows rows() const noexcept;
    float futureWidth() const noexcept;
    void paintFuture (juce::Graphics&, const Hairline&, const Rows&, float nowX, float width);
    int reflexAt (juce::Point<float>) const;
    double span() const noexcept; // seconds shown
    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::Point<float> hoverAt { -1.0f, -1.0f };
};
} // namespace dendrite::ui
