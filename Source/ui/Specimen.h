// The specimen (3.0): the cut's neurons at the places their atlas mapped them (since 3.1 Janelia's male CNS), seen from the
// front, each one a point. A point is as bright as its own firing over the last 300 ms; under
// that, every spike of this session leaves it a little brighter for good (the exposure). A
// neuron that has not fired sits at the floor. Nothing moves unless a neuron fired.
//
// The electrode is the old field's light: where it sits over the brain is feed (up) and space
// (right), the same two parameters. Its tip records the neurons within 25 um of it. Pressing
// anywhere else on the specimen holds the throw (the stimulus), as pressing the dark did.
//
// 3.2: the rail on the right. The imaging method picks which memory of the same spikes the plate
// shows (rig::Recording keeps them all); the small brain under it is this session's 2-DG uptake,
// and pressing it plays how the uptake built up. The goniometer turns the specimen: each neuron
// has its real depth (rig/Depth.h), so the plate is a true projection at any angle.
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Specimen : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Specimen (DendriteProcessor&);
    ~Specimen() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (double nowSeconds); // the window's clock: a press becomes a throw after 80 ms

    // the brain's box in this component, kept at its true aspect (the atlas's, FlyWire's is 816 x 392.8 um)
    juce::Rectangle<float> brainArea() const noexcept { return brain; }
    static constexpr float widthMicrons = flydata::widthMicrons, heightMicrons = flydata::heightMicrons; // the cut's box
    static constexpr float tipMicrons = 25.0f;                            // the electrode's recording radius

    // what the electrode's tip records, measured at the last paint
    struct Recorded
    {
        float xMicrons = 0.0f, yMicrons = 0.0f; // the tip, from the box's left and bottom edges
        int units = 0;                          // neurons within the radius
        float meanHz = 0.0f;                    // their mean rate (each one's 300 ms trace)
        int busiest = -1;                       // the busiest of them (its index in the cut), and its rate
        float busiestHz = 0.0f;
    };
    const Recorded& recorded() const noexcept { return tip; }
    bool stimulating() const noexcept { return touching; }
    double stimulusSeconds() const noexcept { return touching ? clockNow - touchStarted : 0.0; }

    juce::String getTooltip() override;
    void hoverForTools (juce::Point<float> p) noexcept { hoverAt = p; } // the snapshot tool's hand
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void renderPoints (float px);
    void renderMethod (int method, float px);
    void renderUptake (juce::Image& into, const float* uptake, float most, bool frontal, float umPerPixel);
    void paintTracts (juce::Graphics&, const Hairline&);
    void paintRail (juce::Graphics&, const Hairline&);
    void project();
    bool frontal() const noexcept { return std::abs (yawNow) < 0.25f && std::abs (pitchNow) < 0.25f; }
    int methodAt (juce::Point<float>) const noexcept;
    const float* lapsePicture (double& atSeconds, float& most); // the 2-DG frame the plate shows now
    void paintScale (juce::Graphics&, const Hairline&);
    void paintRegions (juce::Graphics&, const Hairline&);
    void paintElectrode (juce::Graphics&, const Hairline&);
    void measureTip();
    int findHover() const;
    void paintHover (juce::Graphics&, const Hairline&);
    juce::Point<float> neuronPoint (int n) const noexcept { return proj[(size_t) n]; }
    juce::Point<float> tipPoint() const noexcept;
    float tipRadius() const noexcept { return tipMicrons * brain.getWidth() / widthMicrons; }
    bool nearTip (juce::Point<float> p) const noexcept { return p.getDistanceFrom (tipPoint()) < tipRadius() + 8.0f; }
    void setThrow (bool on);

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::Rectangle<float> brain;
    juce::Image points; // the neurons, at the screen's own pixels
    juce::Image inset;  // the 2-DG brain on the rail

    // the projection: each neuron's place in microns about the brain's centre (x right, y down,
    // z back), and where it lands on the screen at the goniometer's angles
    struct Place { float x, y, z; };
    std::vector<Place> place;
    std::vector<juce::Point<float>> proj;
    std::vector<float> projUm; // x then y of each, in the plate's microns, for the electrode
    float yawNow = 0.0f, pitchNow = 0.0f;
    // each neuron's strongest outputs, for the tracts: up to three targets and their synapse counts
    static constexpr int tractsPerNeuron = 3;
    std::vector<uint16_t> tractTo;
    std::vector<int16_t> tractWeight;
    std::vector<float> scratch; // the 2-DG film, before its blur

    // the rail
    juce::Rectangle<float> rail, methodList, insetArea, goniometer;
    static constexpr float railRow = 16.0f;
    bool turning = false;
    juce::Point<float> turnFrom;
    float yawFrom = 0.0f, pitchFrom = 0.0f;
    double lapseStarted = -1.0; // when the 2-DG time-lapse began playing (the window's clock)
    static constexpr double lapsePlaySeconds = 8.0;
    Recorded tip;
    int hovered = -1; // the neuron under the hand, at the last paint

    // the parameters, as the window last heard them (the host's value, or the hand's while it drags)
    float feed = DendriteProcessor::defaultFeed, space = DendriteProcessor::defaultSpace;
    bool throwOn = false;
    juce::ParameterAttachment feedLink, spaceLink, throwLink;

    // the hand
    bool hoverTip = false, hoverPlate = false, dragging = false, pending = false, touching = false, fine = false;
    juce::Point<float> grabMouse, grabUnit, pressAt, hoverAt { -1.0f, -1.0f };
    double clockNow = 0.0, pressStarted = 0.0, touchStarted = 0.0;
};
} // namespace dendrite::ui
