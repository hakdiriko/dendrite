#include "Reference.h"
#include "../rig/Identities.h"

namespace dendrite::ui
{
namespace
{
    constexpr float pad = 16.0f;
    const juce::String dot = juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));

    // the neurons' live numbers: this session's spikes, and their mean rate now
    struct Live
    {
        juce::int64 spikes = 0;
        float hz = 0.0f;
        int firing = 0;
    };
    Live liveOf (const rig::Recording& rec, const std::vector<uint16_t>& who)
    {
        Live l;
        for (auto n : who)
        {
            l.spikes += (juce::int64) rec.uptake (n);
            const float hz = rec.trace (n) / (float) rig::Recording::traceSeconds;
            l.hz += hz;
            l.firing += hz >= 1.0f ? 1 : 0;
        }
        if (! who.empty())
            l.hz /= (float) who.size();
        return l;
    }
} // namespace

Reference::Reference (DendriteProcessor& p, Focus& f) : processor (p), focus (f)
{
    setOpaque (false);
    setInterceptsMouseClicks (true, true);
    field.setFont (fonts->at (13.0f));
    field.setColour (juce::TextEditor::backgroundColourId, tone::black);
    field.setColour (juce::TextEditor::textColourId, tone::grey (tone::live));
    field.setColour (juce::TextEditor::outlineColourId, tone::grey (tone::dim));
    field.setColour (juce::TextEditor::focusedOutlineColourId, tone::grey (tone::bright));
    field.setColour (juce::TextEditor::highlightColourId, tone::grey (tone::dim));
    field.setColour (juce::CaretComponent::caretColourId, tone::grey (tone::live));
    field.setTextToShowWhenEmpty ("search: a neuron, a region, a word", tone::grey (tone::dim));
    field.setIndents (8, 6);
    field.addListener (this);
    field.addKeyListener (this);
    addChildComponent (field);
}

Reference::~Reference()
{
    field.removeKeyListener (this);
}

void Reference::resized()
{
    field.setBounds (getLocalBounds().reduced ((int) pad, 0).withTop ((int) pad).withHeight (28).withTrimmedRight (30)); // the × beside it, not over it
}

void Reference::openSearch (const juce::String& typed)
{
    field.setVisible (true);
    field.setText (typed, false);
    textEditorTextChanged (field);
    if (onLayout)
        onLayout();
    field.grabKeyboardFocus();
    field.moveCaretToEnd();
    repaint();
}

void Reference::close()
{
    field.setVisible (false);
    field.clear();
    hits.clear();
    focus.clear();
    if (onLayout)
        onLayout();
    repaint();
}

void Reference::tick()
{
    if (isVisible())
        repaint();
}

void Reference::pickTerm (const juce::String& key)
{
    const auto* e = glossary::term (key);
    if (e == nullptr)
        return;
    std::vector<uint16_t> who;
    if (e->population >= 0)
    {
        who = processor.recording.population (e->population);
        if (e->population == rig::Recording::eyesLeft)
            for (auto n : processor.recording.population (rig::Recording::eyesRight)) who.push_back (n);
        if (e->population == rig::Recording::earsLeft)
            for (auto n : processor.recording.population (rig::Recording::earsRight)) who.push_back (n);
        if (e->population == rig::Recording::steerLeft)
            for (auto n : processor.recording.population (rig::Recording::steerRight)) who.push_back (n);
    }
    else if (e->typeName.isNotEmpty())
        who = glossary::neuronsOfType (glossary::typeIndex (e->typeName));
    if (e->method >= 0)
        processor.recording.method = e->method; // an imaging method picked: the plate shows it
    juce::StringArray words = e->words;
    focus.set (who.empty() ? Focus::Kind::term : Focus::Kind::population, e->key, e->head, std::move (who), words);
    field.setVisible (false);
    scroll = 0.0f;
    repaint();
}

void Reference::pickType (int t)
{
    const auto name = glossary::typeNameOf (t);
    if (name.isEmpty())
        return;
    focus.set (Focus::Kind::type, name, name, glossary::neuronsOfType (t), { name });
    field.setVisible (false);
    scroll = 0.0f;
    repaint();
}

void Reference::textEditorTextChanged (juce::TextEditor&)
{
    hits = glossary::search (field.getText(), 12);
    chosen = 0;
    repaint();
}

void Reference::textEditorReturnKeyPressed (juce::TextEditor&)
{
    choose (chosen);
}

void Reference::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    if (field.getText().isNotEmpty())
        field.clear(), textEditorTextChanged (field);
    else
    {
        field.setVisible (false);
        if (onLayout)
            onLayout();
        repaint();
    }
}

bool Reference::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    if (k == juce::KeyPress::downKey && ! hits.empty())
    {
        chosen = (chosen + 1) % (int) hits.size();
        repaint();
        return true;
    }
    if (k == juce::KeyPress::upKey && ! hits.empty())
    {
        chosen = (chosen + (int) hits.size() - 1) % (int) hits.size();
        repaint();
        return true;
    }
    return false;
}

void Reference::choose (int k)
{
    if (k < 0 || k >= (int) hits.size())
        return;
    const auto h = hits[(size_t) k];
    if (h.termKey.isNotEmpty())
        pickTerm (h.termKey);
    else
        pickType (h.type);
}

float Reference::paragraph (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour, float x, float y, float w)
{
    if (text.isEmpty())
        return 0.0f;
    juce::AttributedString a;
    a.append (text, font, colour);
    a.setLineSpacing (3.0f);
    a.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (a, w);
    layout.draw (g, { x, y, w, layout.getHeight() });
    return layout.getHeight();
}

float Reference::paintEntry (juce::Graphics& g, juce::Rectangle<float> area)
{
    namespace id = rig::identities;
    const auto& rec = processor.recording;
    const float x = area.getX(), w = area.getWidth();
    float y = area.getY();
    links.clear();
    auto mono = [&] (const juce::String& text, float level, float size = type::small) {
        g.setFont (fonts->at (size));
        g.setColour (tone::grey (level));
        y += paragraph (g, text, fonts->at (size), tone::grey (level), x, y, w) + 4.0f;
    };
    auto label = [&] (const juce::String& text) {
        y += 6.0f;
        g.setFont (fonts->at (type::small));
        g.setColour (tone::grey (tone::dim));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        g.drawText (text, juce::Rectangle<float> (x, y, tw + 2.0f, 14.0f), juce::Justification::centredLeft);
        g.setColour (tone::grey (tone::floor));
        g.fillRect (juce::Rectangle<float> (x + tw + 8.0f, y + 7.0f, w - tw - 8.0f, 1.0f));
        y += 18.0f;
    };
    // a row of links: each name opens its own entry
    auto linkRow = [&] (const juce::StringArray& names, bool types) {
        g.setFont (fonts->at (type::small));
        float lx = x;
        for (const auto& raw : names)
        {
            const auto name = types ? raw.upToFirstOccurrenceOf (" ", false, false) : raw;
            const auto shown = raw;
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), shown) + 2.0f;
            if (lx + tw > x + w && lx > x)
            {
                lx = x;
                y += 16.0f;
            }
            const auto r = juce::Rectangle<float> (lx, y, tw, 15.0f);
            const bool over = r.contains (hoverAt);
            g.setColour (tone::grey (over ? tone::live : tone::bright));
            g.drawText (shown, r, juce::Justification::centredLeft);
            g.setColour (tone::grey (over ? tone::bright : tone::dim));
            g.fillRect (juce::Rectangle<float> (lx, y + 14.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name), 1.0f));
            links.push_back ({ r, types ? juce::String() : name, types ? glossary::typeIndex (name) : -1 });
            lx += tw + 14.0f;
        }
        y += 22.0f;
    };

    const glossary::Entry* e = nullptr;
    juce::String head, kind;
    if (focus.kind == Focus::Kind::type)
    {
        // a cell type, from the fly itself; with the words of a term written for it, if there is one
        const int t = glossary::typeIndex (focus.key);
        for (const auto& term : glossary::terms())
            if (term.typeName.isNotEmpty() && focus.key.startsWithIgnoreCase (term.typeName))
                e = &term;
        const auto& m = focus.neurons;
        head = e != nullptr && e->typeName.equalsIgnoreCase (focus.key) ? e->head : focus.key;
        kind = "cell type" + dot + juce::String ((int) m.size()) + (m.size() == 1 ? " neuron" : " neurons");
        if (! m.empty())
        {
            if (const juce::String nt (id::transmitterName[id::transmitter[m.front()]]); nt.isNotEmpty())
                kind << dot << nt;
            if (const juce::String c (id::className[id::superClass[m.front()]]); c.isNotEmpty())
                kind << dot << c.replaceCharacter ('_', ' ');
        }
        if (shownSerial != focus.serial)
        {
            shownSerial = focus.serial;
            inputs = glossary::partners (t, true, 4);
            outputs = glossary::partners (t, false, 4);
        }
    }
    else if (focus.kind == Focus::Kind::event)
    {
        head = focus.title;
        kind = "log" + dot + clock (focus.eventAt).fromFirstOccurrenceOf ("+", false, false);
        e = glossary::term (focus.key);
    }
    else
    {
        e = glossary::term (focus.key);
        if (e != nullptr)
        {
            head = e->head;
            kind = e->kind;
            // a word that stands for neurons (a type, a population): their wiring too, by the
            // neurons the focus holds, so "KC" is every Kenyon cell, not the one type named KC
            if (shownSerial != focus.serial)
            {
                shownSerial = focus.serial;
                inputs = glossary::partners (focus.neurons, true, 4);
                outputs = glossary::partners (focus.neurons, false, 4);
            }
        }
    }

    // the headword, its kind
    y += paragraph (g, head, fonts->text (24.0f), tone::grey (tone::live), x, y, w) + 2.0f;
    y += paragraph (g, kind, fonts->text (14.0f, true), tone::grey (tone::mid), x, y, w) + 10.0f;
    if (e != nullptr && focus.kind != Focus::Kind::event)
        y += paragraph (g, e->body, fonts->text (15.0f), tone::grey (tone::bright), x, y, w) + 8.0f;
    if (focus.kind == Focus::Kind::event && focus.cause > 0)
    {
        const char* paths[] = { "", "JO, the ears \xe2\x86\x92 LPLC2 left \xe2\x86\x92 DNp01 \xe2\x86\x92 the tape",
                                "JO, the ears \xe2\x86\x92 LPLC2 right \xe2\x86\x92 DNp01 \xe2\x86\x92 the tape", "JO, the ears \xe2\x86\x92 DNp01 \xe2\x86\x92 the tape",
                                "the kick \xe2\x86\x92 LPLC2, both eyes \xe2\x86\x92 DNp01 \xe2\x86\x92 the tape" };
        y += paragraph (g, juce::String (juce::CharPointer_UTF8 (paths[juce::jlimit (0, 4, focus.cause)])), fonts->text (15.0f), tone::grey (tone::bright), x, y, w) + 8.0f;
    }

    // its neurons, live
    if (! focus.neurons.empty())
    {
        label ("this session");
        const auto l = liveOf (rec, focus.neurons);
        mono (grouped ((juce::int64) focus.neurons.size()) + " neurons" + dot + grouped (l.firing) + " firing now", tone::bright, type::data);
        mono (grouped (l.spikes) + " spikes" + dot + juce::String (l.hz, 1) + " Hz mean now", tone::bright, type::data);
    }
    if (focus.kind != Focus::Kind::event && ! focus.neurons.empty())
    {
        if (! inputs.isEmpty())
        {
            label ("hears most (synapses)");
            linkRow (inputs, true);
        }
        if (! outputs.isEmpty())
        {
            label ("tells most (synapses)");
            linkRow (outputs, true);
        }
    }
    if (e != nullptr && e->inDendrite.isNotEmpty())
    {
        label ("in dendrite");
        y += paragraph (g, e->inDendrite, fonts->text (15.0f), tone::grey (tone::bright), x, y, w) + 4.0f;
    }
    if (e != nullptr && ! e->see.isEmpty())
    {
        label ("see also");
        juce::StringArray names;
        for (const auto& k : e->see)
            if (glossary::term (k) != nullptr)
                names.add (k);
        linkRow (names, false);
    }
    return y - area.getY();
}

void Reference::paint (juce::Graphics& g)
{
    const Hairline line (g);
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.94f));
    g.fillRect (r);
    g.setColour (tone::grey (tone::dim));
    line.v (r.getRight() - 0.5f, r.getY(), r.getBottom());
    closeBox = juce::Rectangle<float> (r.getRight() - 34.0f, pad + 3.0f, 22.0f, 22.0f);
    g.setFont (fonts->at (14.0f));
    g.setColour (tone::grey (closeBox.contains (hoverAt) ? tone::live : tone::mid));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")), closeBox, juce::Justification::centred);

    if (searching())
    {
        // the hits under the field
        float y = (float) field.getBottom() + 8.0f;
        links.clear();
        if (hits.empty() && field.getText().isNotEmpty())
        {
            g.setFont (fonts->text (14.0f, true));
            g.setColour (tone::grey (tone::mid));
            g.drawText ("nothing by that name in this fly", juce::Rectangle<float> (pad, y, r.getWidth() - 2.0f * pad, 20.0f), juce::Justification::centredLeft);
        }
        for (int k = 0; k < (int) hits.size(); ++k)
        {
            const auto row = juce::Rectangle<float> (pad, y, r.getWidth() - 2.0f * pad, 34.0f);
            const bool on = k == chosen || row.contains (hoverAt);
            if (on)
            {
                g.setColour (tone::grey (0.08f));
                g.fillRect (row);
                g.setColour (tone::grey (tone::live));
                g.fillRect (juce::Rectangle<float> (row.getX(), row.getY() + 6.0f, 2.0f, row.getHeight() - 12.0f));
            }
            const auto& h = hits[(size_t) k];
            g.setFont (fonts->at (13.0f));
            g.setColour (tone::grey (on ? tone::live : tone::bright));
            g.drawText (h.label, row.withTrimmedLeft (10.0f).withHeight (17.0f), juce::Justification::centredLeft, true);
            g.setFont (fonts->text (13.0f, true));
            g.setColour (tone::grey (tone::mid));
            g.drawText ((h.type >= 0 ? "cell type, " : "") + h.detail, row.withTrimmedLeft (10.0f).withTrimmedTop (17.0f), juce::Justification::centredLeft, true);
            links.push_back ({ row, h.termKey, h.type });
            y += 36.0f;
        }
        if (field.getText().isEmpty())
        {
            g.setFont (fonts->text (14.0f, true));
            g.setColour (tone::grey (tone::mid));
            auto hint = juce::Rectangle<float> (pad, y, r.getWidth() - 2.0f * pad, 200.0f);
            paragraph (g, "13,065 neurons of a real fly, by name. Try DNp01, LPLC2, KCg, stimulus, two-photon, fear.",
                       fonts->text (14.0f, true), tone::grey (tone::mid), hint.getX(), hint.getY(), hint.getWidth());
        }
        return;
    }
    if (! focus.active())
        return;
    {
        juce::Graphics::ScopedSaveState keep (g);
        const auto area = r.reduced (pad).withTrimmedTop (14.0f);
        g.reduceClipRegion (area.toNearestInt());
        contentHeight = paintEntry (g, area.translated (0.0f, -scroll));
    }
    // a small search affordance at the top
    g.setFont (fonts->at (type::small));
    g.setColour (tone::grey (tone::dim));
    g.drawText ("/ search", juce::Rectangle<float> (pad, 6.0f, 120.0f, 16.0f), juce::Justification::centredLeft);
}

void Reference::mouseMove (const juce::MouseEvent& e)
{
    hoverAt = e.position;
    bool overLink = closeBox.contains (e.position);
    for (const auto& l : links)
        overLink = overLink || l.area.contains (e.position);
    setMouseCursor (overLink ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void Reference::mouseExit (const juce::MouseEvent&)
{
    hoverAt = { -1.0f, -1.0f };
    repaint();
}

void Reference::mouseDown (const juce::MouseEvent& e)
{
    if (closeBox.contains (e.position))
    {
        close();
        return;
    }
    if (! searching() && juce::Rectangle<float> (pad, 6.0f, 120.0f, 16.0f).contains (e.position))
    {
        openSearch();
        return;
    }
    for (const auto& l : links)
        if (l.area.contains (e.position))
        {
            if (l.term.isNotEmpty())
                pickTerm (l.term);
            else if (l.type >= 0)
                pickType (l.type);
            return;
        }
}

void Reference::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float room = (float) getHeight() - 2.0f * pad - 14.0f;
    scroll = juce::jlimit (0.0f, std::max (0.0f, contentHeight - room), scroll - w.deltaY * 120.0f);
    repaint();
}
} // namespace dendrite::ui
