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
    static constexpr int height = 30;

private:
    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
};
} // namespace dendrite::ui
