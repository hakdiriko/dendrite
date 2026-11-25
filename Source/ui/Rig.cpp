#include "Rig.h"
#include "BinaryData.h"
#include "dsp/FlyData.h"

namespace dendrite::ui
{
Fonts::Fonts()
{
    mono = juce::Typeface::createSystemTypefaceFor (BinaryData::FragmentMonoRegular_ttf, BinaryData::FragmentMonoRegular_ttfSize);
    serif = juce::Typeface::createSystemTypefaceFor (BinaryData::SourceSerif4SmTextRegular_ttf, BinaryData::SourceSerif4SmTextRegular_ttfSize);
    serifItalic = juce::Typeface::createSystemTypefaceFor (BinaryData::SourceSerif4SmTextIt_ttf, BinaryData::SourceSerif4SmTextIt_ttfSize);
}

juce::String grouped (juce::int64 n)
{
    auto digits = juce::String (std::abs (n));
    juce::String out;
    for (int k = 0; k < digits.length(); ++k)
    {
        if (k > 0 && (digits.length() - k) % 3 == 0)
            out << ",";
        out << digits[k];
    }
    return n < 0 ? "-" + out : out;
}

juce::int64 synapseCount()
{
    static const juce::int64 total = [] {
        juce::int64 sum = 0;
        for (int k = 0; k < flydata::synapses; ++k)
            sum += std::abs ((int) flydata::weight[k]);
        return sum;
    }();
    return total;
}

juce::String clock (double seconds)
{
    const auto tenths = (juce::int64) std::floor (std::max (0.0, seconds) * 10.0);
    const auto s = tenths / 10;
    return juce::String::formatted ("t+%02d:%02d:%02d.%d", (int) (s / 3600), (int) ((s / 60) % 60), (int) (s % 60), (int) (tenths % 10));
}

juce::String decibels (float gain)
{
    if (! (gain > 1.0e-6f))
        return "-inf";
    const float db = 20.0f * std::log10 (gain);
    return (db >= 0.05f ? "+" : "") + juce::String (db, 1);
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::TooltipWindow::backgroundColourId, tone::black);
    setColour (juce::TooltipWindow::textColourId, tone::grey (tone::bright));
    setColour (juce::TooltipWindow::outlineColourId, tone::grey (tone::dim));
    setColour (juce::PopupMenu::backgroundColourId, tone::black);
    setColour (juce::PopupMenu::textColourId, tone::grey (tone::mid));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, tone::grey (tone::floor));
    setColour (juce::PopupMenu::highlightedTextColourId, tone::grey (tone::live));
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // one face everywhere, but the reference's serif (3.6) where a font asks for it by name
    if (fonts->serif != nullptr && f.getTypefaceName() == fonts->serif->getName())
        return f.getTypefaceStyle().containsIgnoreCase ("italic") ? fonts->serifItalic : fonts->serif;
    return fonts->mono;
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // two hairlines along the corner, as on a plotter's frame
    g.setColour (tone::grey (isMouseOver || isMouseDragging ? tone::mid : tone::dim));
    Hairline line (g);
    const float fw = (float) w, fh = (float) h;
    line.h (fw * 0.35f, fw - 3.0f, fh - 3.0f);
    line.v (fw - 3.0f, fh * 0.35f, fh - 3.0f);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto font = fonts->at (type::data);
    const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, text)) + 16, h = 22;
    return juce::Rectangle<int> (screenPos.x + 12, screenPos.y + 16, w, h).constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (tone::black);
    g.setColour (tone::grey (tone::dim));
    g.drawRect (0, 0, width, height, 1);
    g.setColour (tone::grey (tone::bright));
    g.setFont (fonts->at (type::data));
    g.drawText (text, 8, 0, width - 16, height, juce::Justification::centredLeft, false);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (tone::black);
    g.setColour (tone::grey (tone::dim));
    g.drawRect (0, 0, width, height, 1);
}

juce::Font LookAndFeel::getPopupMenuFont() { return fonts->at (type::data); }

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    idealHeight = isSeparator ? 7 : 20;
    idealWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + 40;
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                     bool isHighlighted, bool isTicked, bool, const juce::String& text, const juce::String&,
                                     const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (tone::grey (tone::dim));
        Hairline (g).h ((float) area.getX() + 8.0f, (float) area.getRight() - 8.0f, (float) area.getCentreY());
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour (tone::grey (tone::floor));
        g.fillRect (area.reduced (1, 0));
    }
    g.setFont (getPopupMenuFont());
    // the current choice is marked by a square, as on a selector switch, and is the brightest line
    if (isTicked)
    {
        g.setColour (tone::grey (tone::bright));
        g.fillRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ (float) area.getX() + 12.0f, (float) area.getCentreY() }));
    }
    g.setColour (tone::grey (! isActive ? tone::dim : isHighlighted ? tone::live : isTicked ? tone::bright : tone::mid));
    g.drawText (text, area.withTrimmedLeft (24).withTrimmedRight (8), juce::Justification::centredLeft, false);
}
} // namespace dendrite::ui
