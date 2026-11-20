// The raster (3.0): the specimen's populations over the last seconds, one row each, one column
// per tick of the recorder (its real bin, printed). A cell is the population's mean rate in
// that bin, in the specimen's light (1 Hz dim, 100 Hz white); silence is black. It scrolls
// with the specimen's time, so it stands still while the fly rests.
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Raster : public juce::Component, public juce::TooltipClient
{
public:
    explicit Raster (DendriteProcessor&);
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent& e) override { hoverAt = e.position; }
    void mouseExit (const juce::MouseEvent&) override { hoverAt = { -1.0f, -1.0f }; }

    static constexpr int height = 186;
    static constexpr float pixelsPerSecond = 120.0f;

private:
    juce::Rectangle<float> plot() const noexcept;
    static constexpr float rowHeight = 11.0f; // a name per row at 11 px, cells 9 px with a 2 px gap
    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::Point<float> hoverAt { -1.0f, -1.0f };
};
} // namespace dendrite::ui
