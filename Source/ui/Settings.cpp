#include "Settings.h"

namespace dendrite::ui
{
Settings::Settings (DendriteProcessor& p) : processor (p)
{
    setOpaque (true);
}

void Settings::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    g.setColour (tone::grey (tone::dim));
    line.h (0.0f, (float) getWidth(), 0.5f);

    const auto font = fonts->at (type::data);
    g.setFont (font);
    auto width = [&] (const juce::String& s) { return juce::GlyphArrangement::getStringWidth (font, s); };
    const float y = 1.0f, h = (float) getHeight() - 1.0f;
    auto& params = processor.params;
    const bool bypassed = params.getRawParameterValue ("bypass")->load() > 0.5f;
    const bool dryOn = params.getRawParameterValue ("dry")->load() > 0.5f;

    // a field: its name a step down, its value bright (white under the hand)
    auto field = [&] (Field f, float x, const juce::String& name, const juce::String& value, bool rightAligned = false) {
        const float nw = width (name), vw = width (value), gap = 8.0f;
        const float left = rightAligned ? x - nw - gap - vw : x;
        g.setColour (tone::grey (tone::mid));
        g.drawText (name, juce::Rectangle<float> (left, y, nw + 2.0f, h), juce::Justification::centredLeft);
        g.setColour (tone::grey (hover == f ? tone::live : f == output ? (bypassed ? tone::bright : tone::mid) : tone::bright));
        g.drawText (value, juce::Rectangle<float> (left + nw + gap, y, vw + 2.0f, h), juce::Justification::centredLeft);
        areas[(size_t) f] = juce::Rectangle<float> (left - 6.0f, y, nw + gap + vw + 12.0f, h);
        return left + nw + gap + vw;
    };

    float x = 14.0f;
    x = field (itch, x, "itch selector", params.getParameter ("frames")->getCurrentValueAsText()) + 28.0f;
    x = field (heads, x, "heads", params.getParameter ("time")->getCurrentValueAsText()) + 28.0f;
    field (dry, x, "dry", dryOn ? "0 dB" : "off");
    field (output, (float) getWidth() - 14.0f, "output", bypassed ? utf8 ("bypassed \xc2\xb7 recording") : juce::String ("live"), true);
}

Settings::Field Settings::fieldAt (juce::Point<float> p) const
{
    for (int f = 0; f < fields; ++f)
        if (areas[(size_t) f].contains (p))
            return (Field) f;
    return none;
}

void Settings::mouseMove (const juce::MouseEvent& e)
{
    const auto f = fieldAt (e.position);
    hover = f == output ? none : f; // output belongs to the host
    setMouseCursor (hover != none ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void Settings::mouseExit (const juce::MouseEvent&)
{
    hover = none;
    repaint();
}

void Settings::set (const char* id, float value)
{
    auto* p = processor.params.getParameter (id);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (value));
    p->endChangeGesture();
    repaint();
}

void Settings::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;
    switch (fieldAt (e.position))
    {
        case itch:
        {
            const int now = juce::roundToInt (processor.params.getRawParameterValue ("frames")->load());
            juce::PopupMenu menu;
            for (int k = 0; k < DendriteProcessor::itchNames.size(); ++k)
                menu.addItem (k + 1, DendriteProcessor::itchNames[k], true, k == now);
            const auto area = areas[(size_t) itch];
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                                    localAreaToGlobal (area.toNearestInt())).withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards),
                                [safe = juce::Component::SafePointer<Settings> (this)] (int chosen) {
                                    if (safe != nullptr && chosen > 0)
                                        safe->set ("frames", (float) (chosen - 1));
                                });
            break;
        }
        case heads:
            set ("time", (float) ((juce::roundToInt (processor.params.getRawParameterValue ("time")->load()) + 1) % 3));
            break;
        case dry:
            set ("dry", processor.params.getRawParameterValue ("dry")->load() > 0.5f ? 0.0f : 1.0f);
            break;
        case output:
        case none:
        case fields:
            break;
    }
}

juce::String Settings::getTooltip()
{
    switch (hover)
    {
        case itch: return "itch selector: how awake the fly is kept. 1/4 bar is wide awake, 8 bars half asleep, off rests it (click: choose)";
        case heads: return "the delay line's rhythm: dotted, triplet or half (click: the next)";
        case dry: return "the dry path: on at 0 dB, or off for a return track (click: switch)";
        case output:
        case none:
        case fields: return {};
    }
    return {};
}
} // namespace dendrite::ui
