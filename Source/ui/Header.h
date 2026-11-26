// The header (3.0): what the specimen is, and the rig's clock and settings. One line.
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Header : public juce::Component
{
public:
    explicit Header (DendriteProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    static constexpr int height = 30;
    std::function<void()> onSearch; // 3.6: the search, in the header (also "/")

private:
    juce::Rectangle<float> searchArea;
    bool overSearch = false;
    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
};
} // namespace dendrite::ui
