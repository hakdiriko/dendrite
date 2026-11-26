// The rig's look (3.0): black, five steps of grey, one monospace face, hairlines.
// What is alive is brighter, what is dead is dimmer; nothing else makes contrast.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dendrite::ui
{
namespace tone
{
    // the one light hierarchy: firing now, then four steps down to the floor
    constexpr float live = 1.0f, bright = 0.72f, mid = 0.48f, dim = 0.28f, floor = 0.12f;
    inline juce::Colour grey (float level) noexcept
    {
        const auto v = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * level));
        return juce::Colour (v, v, v);
    }
    const juce::Colour black { 0xff000000 };
    // firing to light, on a log scale: 1 Hz at the dim step, 10 Hz between, 100 Hz and up white
    inline float rate (float hz) noexcept
    {
        return dim + (live - dim) * juce::jlimit (0.0f, 1.0f, std::log10 (std::max (hz, 1.0f)) / 2.0f);
    }
    // a scar's light: white when it is made, settled to the mid step within a minute; where
    // scars overlap they add up a little, to a ceiling
    inline float scar (float ageSeconds, int count) noexcept
    {
        const float settle = mid + (live - mid) * std::exp (-std::max (0.0f, ageSeconds) / 15.0f);
        return std::min (live, settle + std::min (0.24f, 0.03f * (float) std::max (0, count - 1)));
    }
} // namespace tone

// Fragment Mono (SIL Open Font License, Resources/fonts), embedded so the window reads the same
// everywhere. Sizes are em sizes in logical pixels, as a browser's font-size.
struct Fonts
{
    Fonts();
    juce::Typeface::Ptr mono;
    juce::Font at (float px) const { return juce::Font (juce::FontOptions (mono).withPointHeight (px)); }
    // 3.6, the reference's voice: Source Serif 4 (small-text optical size, SIL OFL), roman and italic,
    // for its words; numbers, ids and units stay in Fragment Mono
    juce::Typeface::Ptr serif, serifItalic;
    juce::Font text (float px, bool italic = false) const { return juce::Font (juce::FontOptions (italic ? serifItalic : serif).withPointHeight (px)); }
};

// type sizes, chosen at 100% Windows scaling (Hakan: 10 px there was too small to read)
namespace type
{
    constexpr float data = 12.0f;   // readouts, labels, the log
    constexpr float header = 12.0f; // the specimen line and the clock
    constexpr float small = 11.0f;  // tick labels, captions, region names
} // namespace type

// Lines one physical pixel wide, snapped to the pixel grid, whatever the window's scale.
struct Hairline
{
    explicit Hairline (juce::Graphics& graphics) : g (graphics), px (graphics.getInternalContext().getPhysicalPixelScaleFactor()) {}
    float snap (float v) const noexcept { return (std::floor (v * px) + 0.5f) / px; }
    float width() const noexcept { return 1.0f / px; }
    void h (float x0, float x1, float y) const { g.fillRect (juce::Rectangle<float> (x0, snap (y) - 0.5f / px, x1 - x0, 1.0f / px)); }
    void v (float x, float y0, float y1) const { g.fillRect (juce::Rectangle<float> (snap (x) - 0.5f / px, y0, 1.0f / px, y1 - y0)); }
    juce::Graphics& g;
    float px;
};

// "1,034,261"
juce::String grouped (juce::int64 n);
// every synapse in the cut: each connection's weight is its synapse count (counted once)
juce::int64 synapseCount();
// "t+01:12:31.4"
juce::String clock (double seconds);
// a level in dB with one decimal, or "-inf" below -120 dB
juce::String decibels (float gain);
// UTF-8 literal to a String
inline juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    // menus in the same hand: black, one face, the chosen line bright
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 1; }
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
};
} // namespace dendrite::ui
