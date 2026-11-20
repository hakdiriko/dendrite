// The instruments (3.0): the rig's column of readouts beside the specimen. Every line is a value
// the engine or the recorder holds now; nothing is styled to look busy.
//   stimulus     what reaches the specimen: the input and output levels, the host's tempo
//   electrode    what the tip records (the specimen measures it at each paint)
//   delay line   the echo as what it is: two tape heads with their gains per pass, the return,
//                what space sets, the throw, the afterglow, and the output's correlation. No decay
//                time: a formula from the heads' gains alone ignores the tape's filters and ceiling,
//                and would call a tail that dies "sustained".
//   specimen     the fly's own state: how awake the itch selector keeps it, the whole brain's
//                firing, its moods and memory with ten minutes of history, what it is doing
#pragma once

#include "../Processor.h"
#include "Rig.h"
#include "Specimen.h"

namespace dendrite::ui
{
class Instruments : public juce::Component, public juce::TooltipClient
{
public:
    Instruments (DendriteProcessor&, const Specimen&);
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent& e) override { hoverY = e.position.y; }
    void mouseExit (const juce::MouseEvent&) override { hoverY = -1.0f; }

    static constexpr int width = 252, height = 486; // the log takes the rest of the column
private:
    struct Row
    {
        float y;
        juce::String tip;
    };
    DendriteProcessor& processor;
    const Specimen& specimen;
    juce::SharedResourcePointer<Fonts> fonts;
    std::vector<Row> rows; // where each line was drawn, and what its hover says
    float hoverY = -1.0f;
};
} // namespace dendrite::ui
