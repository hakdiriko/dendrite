// The specimen (3.0): the cut's neurons at the places their atlas mapped them (since 3.1 Janelia's male CNS), seen from the
// front, each one a point. A point is as bright as its own firing over the last 300 ms; under
// that, every spike of this session leaves it a little brighter for good (the exposure). A
// neuron that has not fired sits at the floor. Nothing moves unless a neuron fired.
//
// 3.2: the rail on the right. The imaging method picks which memory of the same spikes the plate
// shows (rig::Recording keeps them all); the small brain under it is this session's 2-DG uptake,
// and pressing it plays how the uptake built up. Each neuron has its real depth (rig/Depth.h), so
// the plate is a true projection at any angle: drag the brain to turn it, scroll to magnify it.
//
// 3.3: the light. The brain carries CsChrimson; a spot of 590 nm light sits on it. Its size is
// space, its power is feed (the same two parameters the electrode had), and a press anywhere else
// on the plate is a flash: the throw, and the lit neurons fire for as long as it lasts. Magnified,
// the regions open their own controls: the fly's own numbers (the atlas parameters).
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
    void tick (double nowSeconds); // the window's clock: a still press becomes a flash after 80 ms

    // the brain's box in this component, kept at its true aspect (the atlas's, FlyWire's is 816 x 392.8 um)
    juce::Rectangle<float> brainArea() const noexcept { return brain; }
    static constexpr float widthMicrons = flydata::widthMicrons, heightMicrons = flydata::heightMicrons; // the cut's box
    // the spot's radius for a space value: 8 um (one neuron) to 150 um, on a log scale
    static float spotMicrons (float spaceValue) noexcept { return 8.0f * std::pow (150.0f / 8.0f, juce::jlimit (0.0f, 1.0f, spaceValue)); }

    // what the light covers, measured at the last paint
    struct Recorded
    {
        float xMicrons = 0.0f, yMicrons = 0.0f; // the spot's centre, from the box's left and bottom edges
        float radiusMicrons = 0.0f;
        int units = 0;                          // neurons under it (lit)
        float meanHz = 0.0f;                    // their mean rate (each one's 300 ms trace)
        int busiest = -1;                       // the busiest of them (its index in the cut), and its rate
        float busiestHz = 0.0f;
    };
    const Recorded& recorded() const noexcept { return tip; }
    bool stimulating() const noexcept { return touching; }
    double stimulusSeconds() const noexcept { return touching ? clockNow - touchStarted : 0.0; }

    juce::String getTooltip() override;
    void hoverForTools (juce::Point<float> p) noexcept { hoverAt = p; } // the snapshot tool's hand
    // 3.4, one story: light the path that made a reflex (cause as Timeline::Reflex::cause) for two
    // seconds; a new reflex lights its own path by itself
    void lightPath (int cause, int move);
    void hideElectrodeForTools() noexcept { noElectrode = true; }        // for mockups of other controls
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    void renderPoints (float px);
    void renderMethod (int method, float px);
    // 3.6: the film is crisp, each neuron where it is: grey for any uptake, bright for uptake beyond
    // what ordinary music costs it in the same time (rig/Reference.h); dotPx: a neuron's size in pixels
    void renderUptake (juce::Image& into, const float* uptake, double livedSeconds, bool frontal, float umPerPixel,
                       juce::Point<float> originMicrons = {}, int dotPx = 1);
    juce::String topTypes (const float* uptake, double livedSeconds, int count) const; // the cell types it went to most
    // 3.6: the tracts as the demo drew them: the strongest connections as curves, a dim skeleton of
    // the wiring (redrawn when the view changes) and a live layer lit by each sender's last second
    void renderTracts (juce::Image::BitmapData&, int w, int h, float px);
    void lightTissue (juce::Image::BitmapData&, int w, int h, float px, int dot); // the light on the tissue
    void paintRail (juce::Graphics&, const Hairline&);
    void project();
    bool frontal() const noexcept { return std::abs (yawNow) < 0.25f && std::abs (pitchNow) < 0.25f; }
    int methodAt (juce::Point<float>) const noexcept;
    const float* lapsePicture (double& atSeconds, double& livedSeconds); // the 2-DG frame the plate shows now
    void paintScale (juce::Graphics&, const Hairline&);
    void paintRegions (juce::Graphics&, const Hairline&);
    void paintPath (juce::Graphics&, const Hairline&);
    int pathCause = 0, pathMove = 0, reflexesLit = 0;
    double pathFrom = -10.0, clockSeconds = 0.0; // the window's clock (tick)
    void paintLight (juce::Graphics&, const Hairline&);
    void measureLight();
    int findHover() const;
    void paintHover (juce::Graphics&, const Hairline&);
    juce::Point<float> neuronPoint (int n) const noexcept { return proj[(size_t) n]; }
    float pixelsPerMicron() const noexcept { return brain.getWidth() / widthMicrons * zoomNow; }
    // a place in the plate's microns (after the turn) to the screen, through the magnification
    juce::Point<float> toScreen (float ux, float uy) const noexcept
    {
        const float s = pixelsPerMicron();
        return { brain.getCentreX() + (ux - zoomCx) * s, brain.getCentreY() + (uy - zoomCy) * s };
    }
    juce::Point<float> toMicrons (juce::Point<float> p) const noexcept
    {
        const float s = pixelsPerMicron();
        return { zoomCx + (p.x - brain.getCentreX()) / s, zoomCy + (p.y - brain.getCentreY()) / s };
    }
    void setZoom (float z, juce::Point<float> keepAt); // keepAt: a screen point that stays over the same place
    void viewHome();                                   // frontal, whole, eased
    void clampField();
    void storeField();
    void adoptField();                                 // the saved view, when the host loads a session
    void turnBy (float dYaw, float dPitch);
    void setThrow (bool on);

    // the light on screen, and the hand's parts of it
    juce::Point<float> spotPoint() const noexcept;
    float spotRadius() const noexcept { return spotMicrons (space) * pixelsPerMicron(); }
    enum class Part { none, centre, ring, inside, dial };
    Part partAt (juce::Point<float>) const noexcept;

    // the atlas: a region's controls, open when magnified enough to see it
    struct Site
    {
        const char* name;
        std::vector<uint16_t> neurons;
        int params[2];                // indices into DendriteProcessor::tasteParams, -1: none
        juce::Point<float> at;        // where it sits on screen, at the last paint
        bool open = false;
        juce::Rectangle<float> dials[2];
    };
    std::vector<Site> sites;
    void paintSites (juce::Graphics&, const Hairline&);
    int dialAt (juce::Point<float>, int& which) const noexcept;
    static constexpr float sitesOpenAt = 2.5f; // the magnification from which regions open
    std::array<juce::RangedAudioParameter*, 8> taste {};

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::Rectangle<float> brain;
    juce::Image points; // the neurons, at the screen's own pixels
    juce::Image inset;  // the 2-DG brain on the rail

    // the projection: each neuron's place in microns about the brain's centre (x right, y down,
    // z back), and where it lands on the screen at the stage's angles
    struct Place { float x, y, z; };
    std::vector<Place> place;
    std::vector<juce::Point<float>> proj;
    std::vector<float> projUm;    // x then y of each, in the plate's microns
    std::vector<float> projDepth; // 0 nearest the viewer, 1 furthest (only while turned)
    float placeRadius = 1.0f;     // the brain's reach from its centre, for depth cueing
    float zoomNow = 1.0f, zoomCx = 0.5f * widthMicrons, zoomCy = 0.5f * heightMicrons;
    float zoomGoal = 1.0f, goalCx = 0.0f, goalCy = 0.0f, seenZoom = -1.0f, seenX = -1.0f, seenY = -1.0f;
    bool easing = false;
    // turning: dragging the brain (or the right button); a flick keeps it turning
    bool turning = false;
    juce::Point<float> lastTurnAt;
    double lastTurnTime = 0.0, lastTick = -1.0;
    float spinYaw = 0.0f, spinPitch = 0.0f;     // degrees a second
    float turnedYaw = 0.0f, turnedPitch = 0.0f; // turned since the last speed was taken
    float yawNow = 0.0f, pitchNow = 0.0f;
    bool panning = false;
    juce::Point<float> panFrom, panCentreFrom;
    // the tracts (3.6): the cut's strongest connections (at most 24 from one neuron), each a curve
    struct Tract { uint16_t from, to; float strength; }; // strength: log synapses, 0..1
    std::vector<Tract> tracts;
    std::vector<float> skeleton, tractLight; // the wiring at the screen's pixels, and this frame's light
    std::array<float, 7> skeletonFor {};     // the view it was drawn for
    float skeletonScale = 1.0f;              // its densest pixels (99.5th percentile)
    static constexpr int tractCount = 24000, busyCount = 25000;
    std::vector<Tract> busy; // the busiest connections now (strength: synapses x the sender's spikes, last second)
    double busyAt = -1.0;

    // the rail
    juce::Rectangle<float> rail, methodList, insetArea;
    static constexpr float railRow = 16.0f;
    double lapseStarted = -1.0; // when the 2-DG time-lapse began playing (the window's clock)
    static constexpr double lapsePlaySeconds = 8.0;
    Recorded tip;
    int hovered = -1; // the neuron under the hand, at the last paint

    // the lit neurons, as last sent to the fly
    std::vector<uint16_t> lit, litSent;
    bool litPending = true;

    // the parameters, as the window last heard them (the host's value, or the hand's while it drags)
    float feed = DendriteProcessor::defaultFeed, space = DendriteProcessor::defaultSpace;
    bool throwOn = false;
    juce::ParameterAttachment feedLink, spaceLink, throwLink;

    // the hand
    bool noElectrode = false;
    Part hoverPart = Part::none, dragPart = Part::none;
    int dragSite = -1, dragDial = 0;
    float dragDialFrom = 0.0f;
    bool hoverPlate = false, pending = false, touching = false, fine = false;
    juce::Point<float> grabMouse, grabUnit, pressAt, hoverAt { -1.0f, -1.0f };
    float grabValue = 0.0f;
    double clockNow = 0.0, pressStarted = 0.0, touchStarted = 0.0;
};
} // namespace dendrite::ui
