#include "Raster.h"
#include "rig/Recording.h"

namespace dendrite::ui
{
using rig::Recording;

Raster::Raster (DendriteProcessor& p) : processor (p)
{
    setOpaque (true);
}

juce::Rectangle<float> Raster::plot() const noexcept
{
    return { 150.0f, 14.0f, std::max (10.0f, (float) getWidth() - 150.0f - 24.0f), rowHeight * (float) Recording::populations };
}

void Raster::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    const auto& rec = processor.recording;
    const auto area = plot();
    const auto small = fonts->at (type::small);
    g.setFont (small);
    g.setColour (tone::grey (tone::dim));
    line.h (0.0f, (float) getWidth(), 0.5f);

    // the rows' names and sizes, so a dense row reads as many neurons, not as loud ones
    for (int p = 0; p < Recording::populations; ++p)
    {
        const float y = area.getY() + rowHeight * (float) p;
        g.setColour (tone::grey (tone::mid));
        g.drawText (Recording::populationName (p), juce::Rectangle<float> (14.0f, y - 1.0f, 90.0f, rowHeight), juce::Justification::centredLeft);
        g.setColour (tone::grey (tone::dim));
        g.drawText (grouped (rec.populationSize (p)), juce::Rectangle<float> (100.0f, y - 1.0f, area.getX() - 108.0f, rowHeight), juce::Justification::centredRight);
    }

    // the cells: each tick from its time back to the one before it
    const int n = rec.rasterLength();
    const double newest = n > 0 ? rec.rasterTime (0) : 0.0;
    const float px = line.px;
    std::vector<double> bins;
    for (int k = 0; k + 1 < n; ++k)
    {
        const double t1 = rec.rasterTime (k), t0 = rec.rasterTime (k + 1);
        const float x1 = area.getRight() - (float) ((newest - t1) * pixelsPerSecond);
        if (x1 < area.getX())
            break;
        const float x0 = std::max (area.getX(), area.getRight() - (float) ((newest - t0) * pixelsPerSecond));
        if (bins.size() < 64)
            bins.push_back (t1 - t0);
        // whole physical pixels, so neighbouring bins neither overlap nor leave seams
        const float a = std::round (x0 * px) / px, b = std::round (x1 * px) / px;
        if (b <= a)
            continue;
        for (int p = 0; p < Recording::populations; ++p)
        {
            const float hz = rec.rasterRate (p, k);
            if (hz <= 0.0f)
                continue;
            g.setColour (tone::grey (hz < 1.0f ? tone::floor : tone::rate (hz)));
            g.fillRect (juce::Rectangle<float> (a, area.getY() + rowHeight * (float) p, b - a, rowHeight - 2.0f));
        }
    }

    // at rest no time passes for the specimen: the raster says so instead of scrolling an empty record
    if (juce::roundToInt (processor.params.getRawParameterValue ("frames")->load()) == 0)
    {
        g.setColour (tone::grey (tone::mid));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("at rest \xc2\xb7 its brain does not step")), area.withTrimmedRight (8.0f),
                    juce::Justification::centredRight);
    }

    // the time axis: a tick every second back from now, a 1 s scale bar, and the bin as measured
    const float axis = area.getBottom() + 4.0f;
    g.setColour (tone::grey (tone::dim));
    line.h (area.getX(), area.getRight(), axis);
    for (int s = 0;; ++s)
    {
        const float x = area.getRight() - (float) s * pixelsPerSecond;
        if (x < area.getX())
            break;
        line.v (x, axis, axis + (s % 5 == 0 ? 6.0f : 3.0f));
    }
    g.setColour (tone::grey (tone::mid));
    g.drawText ("now", juce::Rectangle<float> (area.getRight() - 40.0f, axis + 6.0f, 40.0f, 14.0f), juce::Justification::centredRight);
    const float bar = area.getX();
    g.setColour (tone::grey (tone::bright));
    line.h (bar, bar + pixelsPerSecond, axis + 14.0f);
    line.v (bar, axis + 11.0f, axis + 17.0f);
    line.v (bar + pixelsPerSecond, axis + 11.0f, axis + 17.0f);
    g.setColour (tone::grey (tone::mid));
    juce::String caption = "1 s";
    if (! bins.empty())
    {
        std::nth_element (bins.begin(), bins.begin() + (long) (bins.size() / 2), bins.end());
        caption << juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   bin ")) << juce::roundToInt (bins[bins.size() / 2] * 1000.0) << " ms"
                << juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   population rate"));
    }
    g.drawText (caption, juce::Rectangle<float> (bar + pixelsPerSecond + 8.0f, axis + 7.0f, 400.0f, 14.0f), juce::Justification::centredLeft);
}

juce::String Raster::getTooltip()
{
    const auto area = plot();
    if (! area.expanded (0.0f, 3.0f).withLeft (0.0f).contains (hoverAt))
        return {};
    const int p = juce::jlimit (0, Recording::populations - 1, (int) ((hoverAt.y - area.getY()) / rowHeight));
    const auto& rec = processor.recording;
    juce::String text = juce::String (Recording::populationName (p)) + ": " + grouped (rec.populationSize (p)) + " neurons";
    if (hoverAt.x >= area.getX() && rec.rasterLength() > 0)
    {
        const double back = (area.getRight() - hoverAt.x) / pixelsPerSecond;
        const double newest = rec.rasterTime (0);
        for (int k = 0; k < rec.rasterLength(); ++k)
            if (newest - rec.rasterTime (k) >= back)
            {
                text << ", " << juce::String (rec.rasterRate (p, k), 1) << " Hz each, " << juce::String (back, 1) << " s ago";
                break;
            }
    }
    return text;
}
} // namespace dendrite::ui
