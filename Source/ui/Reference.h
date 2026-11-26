// The reference (3.6): search and the entry, over the specimen's left side. "/" or the header's
// search opens a field; typing finds terms, readouts and the fly's cell types ("stimulus", "DNp01",
// "KCg"); picking one makes it the focus, and its entry opens: the word in Source Serif 4, the
// numbers in Fragment Mono, live from the fly. Every name in an entry is a link to its own.
#pragma once

#include "../Processor.h"
#include "Focus.h"
#include "Glossary.h"
#include "Rig.h"

namespace dendrite::ui
{
class Reference : public juce::Component, private juce::TextEditor::Listener, private juce::KeyListener
{
public:
    Reference (DendriteProcessor&, Focus&);
    ~Reference() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void openSearch (const juce::String& typed = {}); // the field, focused
    void close();                                     // the field and the entry, and the focus
    bool searching() const noexcept { return field.isVisible(); }
    bool wanted() const noexcept { return searching() || focus.active(); } // the editor shows it while true

    // pick: a term by key, or a cell type by index (both make the focus)
    void pickTerm (const juce::String& key);
    void pickType (int typeIndex);
    void tick(); // ten times a second: the live numbers

    static constexpr int width = 320;
    std::function<void()> onLayout; // shown or hidden: the editor places it and the specimen makes room

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void choose (int hit);

    struct Link
    {
        juce::Rectangle<float> area;
        juce::String term;
        int type = -1;
    };
    float paintEntry (juce::Graphics&, juce::Rectangle<float> area); // returns the height it used
    float paragraph (juce::Graphics&, const juce::String& text, const juce::Font&, juce::Colour, float x, float y, float w);

    DendriteProcessor& processor;
    Focus& focus;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::TextEditor field;
    std::vector<glossary::Hit> hits;
    int chosen = 0;
    std::vector<Link> links;
    juce::Rectangle<float> closeBox, card;
    juce::Point<float> hoverAt { -1.0f, -1.0f };
    float scroll = 0.0f, contentHeight = 0.0f;
    int shownSerial = -1;
    juce::StringArray inputs, outputs; // the focused type's strongest partners (computed once per focus)
};
} // namespace dendrite::ui
