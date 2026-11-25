#include "Settings.h"

namespace dendrite::ui
{
Settings::Settings (DendriteProcessor& p) : processor (p)
{
    setOpaque (true);
}

namespace
{
    // the itch choices as the scale writes them (the host keeps its long names)
    const char* const itchShort[] = { "off", "\xc2\xbc", "\xc2\xbd", "\xc2\xbe", "1", "1\xc2\xbc",
                                      "1\xc2\xbd", "1\xc2\xbe", "2", "3", "4", "8" };
    juce::String awakeText (int k)
    {
        return k <= 0 ? juce::String ("rests") : "awake " + juce::String (dendrite::Engine::awakeFor (k), 2);
    }
} // namespace

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
    const int itchNow = juce::jlimit (0, itchChoices - 1, juce::roundToInt (params.getRawParameterValue ("frames")->load()));
    const int headsNow = juce::jlimit (0, 2, juce::roundToInt (params.getRawParameterValue ("time")->load()));
    auto text = [&] (const juce::String& s, float x, float level) {
        g.setColour (tone::grey (level));
        g.drawText (s, juce::Rectangle<float> (x, y, width (s) + 2.0f, h), juce::Justification::centredLeft);
        return x + width (s);
    };

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
    if (open)
    {
        // the scale, written out in the line: wide awake on the left, the choice white and underlined
        const float start = x;
        x = text ("itch selector", x, hover == itch && itchHover < 0 ? tone::live : tone::mid) + 14.0f;
        float widest = 0.0f;
        for (auto* s : itchShort)
            widest = std::max (widest, width (utf8 (s)));
        itchStep = widest + 14.0f;
        for (int k = 0; k < itchChoices; ++k)
        {
            const auto s = utf8 (itchShort[k]);
            const float cx = x + itchStep * ((float) k + 0.5f);
            itchX[(size_t) k] = cx;
            const float level = k == itchNow ? tone::live : k == itchHover ? tone::bright : k == 0 ? tone::dim : tone::mid;
            text (s, cx - 0.5f * width (s), level);
            g.setColour (tone::grey (k == itchNow ? tone::live : tone::dim));
            line.v (cx, k == itchNow ? 4.0f : 6.0f, k == itchNow ? 9.0f : 8.0f);
            if (k == itchNow)
                line.h (cx - 0.5f * itchStep + 4.0f, cx + 0.5f * itchStep - 4.0f, h - 3.0f);
        }
        x += itchStep * (float) itchChoices + 4.0f;
        x = text ("bars", x, tone::dim) + 22.0f;
        text (awakeText (itchHover >= 0 ? itchHover : itchNow), x, itchHover >= 0 && itchHover != itchNow ? tone::bright : tone::live);
        areas[(size_t) itch] = juce::Rectangle<float> (start - 6.0f, y, x - start + 120.0f, h);
        areas[(size_t) heads] = areas[(size_t) dry] = {};
    }
    else
    {
        x = field (itch, x, "itch selector", utf8 (itchShort[itchNow]) + (itchNow == 0 ? "" : itchNow <= 4 ? " bar" : " bars")) + 28.0f;

        // heads: all three rhythms written out, the one playing bright
        const float start = x;
        x = text ("heads", x, tone::mid) + 8.0f;
        const char* const names[] = { "dotted", "triplet", "half" };
        for (int k = 0; k < 3; ++k)
        {
            if (k > 0)
                x = text (utf8 (" \xc2\xb7 "), x, tone::floor);
            const float level = k == headsNow ? (headsHover == k ? tone::live : tone::bright) : headsHover == k ? tone::bright : tone::dim;
            const float x0 = x;
            x = text (names[k], x, level);
            headsAreas[(size_t) k] = juce::Rectangle<float> (x0 - 4.0f, y, x - x0 + 8.0f, h);
        }
        areas[(size_t) heads] = juce::Rectangle<float> (start - 6.0f, y, x - start + 12.0f, h);
        x += 28.0f;
        field (dry, x, "dry", dryOn ? "0 dB" : "off");
    }
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
    itchHover = open ? itchAt (e.position.x) : -1;
    headsHover = f == heads ? headsAt (e.position) : -1;
    if (open)
    {
        if (f == itch)
            stopTimer(); // stays out while the hand is on it
        else
            startTimer (1500);
    }
    setMouseCursor (hover != none ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void Settings::mouseExit (const juce::MouseEvent&)
{
    hover = none;
    itchHover = headsHover = -1;
    if (open && ! scrubbing)
        startTimer (1200);
    repaint();
}

void Settings::set (const char* id, float value)
{
    auto* p = processor.params.getParameter (id);
    if (juce::approximatelyEqual (p->convertFrom0to1 (p->getValue()), value))
        return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (value));
    p->endChangeGesture();
    repaint();
}

int Settings::itchAt (float x) const
{
    if (! open || itchStep <= 0.0f)
        return -1;
    const float first = itchX[0] - 0.5f * itchStep;
    const int k = (int) std::floor ((x - first) / itchStep);
    return k >= 0 && k < itchChoices ? k : -1;
}

int Settings::headsAt (juce::Point<float> p) const
{
    for (int k = 0; k < 3; ++k)
        if (headsAreas[(size_t) k].contains (p))
            return k;
    return -1;
}

void Settings::close()
{
    stopTimer();
    open = scrubbing = false;
    itchHover = -1;
    setWantsKeyboardFocus (false);
    repaint();
}

void Settings::timerCallback()
{
    close();
}

void Settings::mouseDown (const juce::MouseEvent& e)
{
    const int k = itchAt (e.position.x);
    if (open && k >= 0)
    {
        scrubbing = true;
        stopTimer();
        set ("frames", (float) k);
    }
}

void Settings::mouseDrag (const juce::MouseEvent& e)
{
    if (! scrubbing)
        return;
    // along the scale, held at its ends
    const float first = itchX[0], last = itchX[(size_t) itchChoices - 1];
    const int k = itchAt (juce::jlimit (first, last, e.position.x));
    itchHover = k;
    if (k >= 0)
        set ("frames", (float) k);
    repaint();
}

void Settings::mouseUp (const juce::MouseEvent& e)
{
    if (scrubbing)
    {
        scrubbing = false;
        startTimer (900); // long enough to see the choice settle white
        return;
    }
    if (! e.mouseWasClicked())
        return;
    switch (fieldAt (e.position))
    {
        case itch:
            if (open)
                close(); // a click on the name folds the scale away
            else
            {
                open = true;
                itchHover = -1;
                setWantsKeyboardFocus (true);
                grabKeyboardFocus();
                startTimer (6000);
                repaint();
            }
            break;
        case heads:
            if (const int k = headsAt (e.position); k >= 0)
                set ("time", (float) k);
            break;
        case dry:
            set ("dry", processor.params.getRawParameterValue ("dry")->load() > 0.5f ? 0.0f : 1.0f);
            break;
        case output:
        case fields:
            break;
        case none:
            if (open)
                close();
            break;
    }
}

void Settings::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (fieldAt (e.position) != itch)
        return;
    // one step per notch; a trackpad's stream of small deltas adds up to the same
    wheelSum += wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
    if (std::abs (wheelSum) < 0.1f)
        return;
    const int now = juce::roundToInt (processor.params.getRawParameterValue ("frames")->load());
    set ("frames", (float) juce::jlimit (0, itchChoices - 1, now + (wheelSum > 0.0f ? 1 : -1)));
    wheelSum = 0.0f;
    if (open)
        startTimer (2500);
}

bool Settings::keyPressed (const juce::KeyPress& key)
{
    if (! open)
        return false;
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }
    const int now = juce::roundToInt (processor.params.getRawParameterValue ("frames")->load());
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        set ("frames", (float) juce::jlimit (0, itchChoices - 1, now + (key == juce::KeyPress::rightKey ? 1 : -1)));
        startTimer (2500);
        return true;
    }
    return false;
}

juce::String Settings::getTooltip()
{
    switch (hover)
    {
        case itch: return open ? juce::String() : juce::String ("itch selector: how awake the fly is kept. 1/4 bar is wide awake, 8 bars half asleep, off rests it (click: the scale; wheel: a step)");
        case heads: return "the delay line's rhythm: dotted, triplet or half (click one)";
        case dry: return "the dry path: on at 0 dB, or off for a return track (click: switch)";
        case output:
        case none:
        case fields: return {};
    }
    return {};
}
} // namespace dendrite::ui
