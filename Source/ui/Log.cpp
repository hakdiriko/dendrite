#include "Log.h"
#include "rig/Recording.h"

namespace dendrite::ui
{
void focusHostWindow (void* nativeHandle); // HostFocus.cpp

namespace
{
    // "03:12.4", or "1:03:12.4" past the first hour: the session clock, as short as it can be
    juce::String stamp (double seconds)
    {
        const auto tenths = (juce::int64) std::floor (std::max (0.0, seconds) * 10.0);
        const auto s = tenths / 10;
        if (s < 3600)
            return juce::String::formatted ("%02d:%02d.%d", (int) (s / 60), (int) (s % 60), (int) (tenths % 10));
        return juce::String::formatted ("%d:%02d:%02d.%d", (int) (s / 3600), (int) ((s / 60) % 60), (int) (s % 60), (int) (tenths % 10));
    }
} // namespace

Log::Log (DendriteProcessor& p) : processor (p)
{
    setOpaque (true);
}

bool Log::held (uint32_t mark) const
{
    return mark != 0 && processor.keep.find (mark) != nullptr;
}

void Log::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    g.setColour (tone::grey (tone::dim));
    line.v (0.0f, 0.0f, (float) getHeight());

    const float x0 = 16.0f, x1 = (float) getWidth() - 14.0f, pitch = 14.0f;
    const auto small = fonts->at (type::small);
    g.setFont (small);
    auto width = [&] (const juce::String& s) { return juce::GlyphArrangement::getStringWidth (small, s); };

    // the heading, as the instruments' are
    g.setColour (tone::grey (tone::mid));
    const auto& events = processor.recording.events();
    const auto title = "log" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (events.empty() ? 0 : (int) events.back().id) + " entries";
    g.drawText (title, juce::Rectangle<float> (x0, 6.0f, width (title) + 2.0f, 14.0f), juce::Justification::centredLeft);
    g.setColour (tone::grey (tone::dim));
    line.h (x0 + width (title) + 8.0f, x1, 13.0f);

    // newest at the bottom, full white; each older line a step dimmer, down to the mid step
    lines.clear();
    const float top = 26.0f;
    float y = (float) getHeight() - 6.0f - pitch;
    int age = 0;
    const auto hover = lineAt (mouseAt);
    for (auto it = events.rbegin(); it != events.rend() && y >= top; ++it, ++age, y -= pitch)
    {
        const bool handle = held (it->mark);
        const bool lit = hover != nullptr && std::abs (hover->y - y) < 0.5f && handle;
        const float level = lit ? tone::live : std::max (tone::mid, tone::live - 0.04f * (float) age);
        const auto when = stamp (it->at);
        const float ww = width (when);
        // a line keep can still give back is marked at its left edge
        if (handle)
        {
            g.setColour (tone::grey (lit ? tone::live : tone::mid));
            g.fillRect (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ x0 - 7.0f, y + 0.5f * pitch }));
        }
        g.setColour (tone::grey (level));
        g.drawText (it->text, juce::Rectangle<float> (x0, y, x1 - x0 - ww - 8.0f, pitch), juce::Justification::centredLeft, true);
        g.setColour (tone::grey (std::min (level, tone::mid)));
        g.drawText (when, juce::Rectangle<float> (x1 - ww - 1.0f, y, ww + 2.0f, pitch), juce::Justification::centredRight);
        lines.push_back ({ y, it->mark, it->text });
    }
}

const Log::Line* Log::lineAt (juce::Point<float> p) const
{
    for (const auto& l : lines)
        if (p.y >= l.y && p.y < l.y + 14.0f && p.x >= 0.0f)
            return &l;
    return nullptr;
}

juce::String Log::getTooltip()
{
    const auto* l = lineAt (mouseAt);
    if (l == nullptr)
        return {};
    if (held (l->mark))
        return l->text.startsWith ("reflex") || l->text.startsWith ("itch") ? "pull this itch into Live, bar to bar (shift: with everything)"
                                                                             : "pull this into Live, bar to bar";
    if (l->mark != 0)
        return "older than keep's last minute: no longer on tape";
    return {};
}

void Log::mouseMove (const juce::MouseEvent& e)
{
    mouseAt = e.position;
    const auto* l = lineAt (mouseAt);
    setMouseCursor (l != nullptr && held (l->mark) ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void Log::mouseExit (const juce::MouseEvent&)
{
    mouseAt = { -1.0f, -1.0f };
    repaint();
}

void Log::mouseDown (const juce::MouseEvent& e)
{
    const auto* l = lineAt (e.position);
    pressed = l != nullptr && held (l->mark);
    pressedMark = pressed ? l->mark : 0;
    shift = e.mods.isShiftDown();
    dragged = false;
}

void Log::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed || dragged || e.getDistanceFromDragStart() < 6)
        return;
    dragged = true;
    auto& keep = processor.keep;
    const auto* m = keep.find (pressedMark);
    if (m == nullptr)
        return;
    const auto take = keep.takeMark (*m, shift);
    if (! take)
        return;
    const auto file = keep.write (*take);
    if (! file.existsAsFile())
        return;
    // Live needs its own window focused before a drop from a plug-in window (Windows)
    if (juce::PluginHostType().isAbletonLive())
        focusHostWindow (getPeer() != nullptr ? getPeer()->getNativeHandle() : nullptr);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [] {});
}

void Log::mouseUp (const juce::MouseEvent&)
{
    pressed = dragged = false;
}
} // namespace dendrite::ui
