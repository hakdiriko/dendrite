// The specimen (3.0): the fly's 11,705 neurons at the places FlyWire mapped them, seen from the
// front, each one a point. A point is as bright as its own firing over the last 300 ms; under
// that, every spike of this session leaves it a little brighter for good (the exposure). A
// neuron that has not fired sits at the floor. Nothing moves unless a neuron fired.
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Specimen : public juce::Component
{
public:
    explicit Specimen (DendriteProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    // the brain's box in this component, kept at its true aspect (816 x 392.8 um)
    juce::Rectangle<float> brainArea() const noexcept { return brain; }
    static constexpr float widthMicrons = 816.0f, heightMicrons = 392.8f; // FlyWire 4 nm voxels, the cut's box

private:
    void renderPoints (float px);
    void paintScale (juce::Graphics&, const Hairline&);
    void paintRegions (juce::Graphics&, const Hairline&);

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::Rectangle<float> brain;
    juce::Image points; // the neurons, at the screen's own pixels
};
} // namespace dendrite::ui
