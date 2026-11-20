#include "Specimen.h"
#include "rig/Recording.h"

namespace dendrite::ui
{
namespace
{
    constexpr int neurons = flydata::neurons;

    // a 1-2-5 length in um near a wanted length
    float niceMicrons (float wanted)
    {
        const float decade = std::pow (10.0f, std::floor (std::log10 (std::max (wanted, 1.0e-3f))));
        float best = decade;
        for (float m : { 1.0f, 2.0f, 5.0f, 10.0f })
            if (std::abs (std::log (m * decade / wanted)) < std::abs (std::log (best / wanted)))
                best = m * decade;
        return best;
    }

    juce::String microns (float um) { return juce::String (juce::roundToInt (um)) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb5m")); }

    // where FlyData's anchors are, and what the rig calls them
    struct Region
    {
        const char* anchor;
        const char* name;
        bool above;
    };
    const Region regions[] = {
        { "eyeLeft", "LPLC2 left, looming", true },
        { "mushroomBody", "KC, mushroom body left", true },
        { "eyeRight", "LPLC2 right, looming", true },
        { "ears", "JO, johnston's organ", false },
        { "giantFiber", "DNp01, giant fiber", false },
        { "grooming", "DNg11, grooming", false },
    };

    // firing to light, on a log scale: 1 Hz at the dim step, 10 Hz between, 100 Hz and up white
    float levelFor (float hz) noexcept
    {
        return tone::dim + (tone::live - tone::dim) * juce::jlimit (0.0f, 1.0f, std::log10 (std::max (hz, 1.0f)) / 2.0f);
    }

    juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
} // namespace

Specimen::Specimen (DendriteProcessor& p) : processor (p)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

void Specimen::resized()
{
    // room for the region names above and below, the ticks and scale on the left and bottom
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (52.0f);
    r.removeFromRight (36.0f);
    r.removeFromTop (72.0f);
    r.removeFromBottom (84.0f);
    const float aspect = widthMicrons / heightMicrons;
    const float w = std::min (r.getWidth(), r.getHeight() * aspect);
    brain = juce::Rectangle<float> (w, w / aspect).withCentre (r.getCentre());
}

void Specimen::renderPoints (float px)
{
    const int w = std::max (1, juce::roundToInt (brain.getWidth() * px)), h = std::max (1, juce::roundToInt (brain.getHeight() * px));
    if (points.getWidth() != w || points.getHeight() != h)
        points = juce::Image (juce::Image::RGB, w, h, true);
    const auto& rec = processor.recording;
    const float most = (float) std::log1p ((double) std::max<uint32_t> (1u, rec.mostExposed()));
    // one logical pixel at the default window (816 um across ~790 px: about 1 um a pixel), growing
    // with the window so a large picture keeps its density; always whole physical pixels
    const int dot = std::max (1, juce::roundToInt (px * std::max (1.0f, brain.getWidth() / 790.0f)));

    juce::Image::BitmapData bits (points, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y)
        std::memset (bits.getLinePointer (y), 0, (size_t) (bits.pixelStride * w));

    for (int n = 0; n < neurons; ++n)
    {
        // the floor, raised by this session's exposure (to the dim step), lit by firing now
        const auto e = rec.exposure (n);
        float level = tone::floor;
        if (e > 0)
            level += (tone::dim - tone::floor) * (float) std::log1p ((double) e) / most;
        const float hz = rec.trace (n) / (float) rig::Recording::traceSeconds;
        if (hz >= 1.0f)
            level = std::max (level, levelFor (hz));
        const auto v = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * level));

        const int x0 = juce::jlimit (0, w - dot, (int) ((float) flydata::neuronX[n] / 65535.0f * (float) (w - dot)));
        const int y0 = juce::jlimit (0, h - dot, (int) ((float) flydata::neuronY[n] / 65535.0f * (float) (h - dot)));
        for (int y = y0; y < y0 + dot; ++y)
            for (int x = x0; x < x0 + dot; ++x)
            {
                auto* p = bits.getPixelPointer (x, y);
                if (p[0] < v) // where two neurons share a pixel, the brighter one shows
                    p[0] = p[1] = p[2] = v;
            }
    }
}

void Specimen::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    renderPoints (line.px);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed (points, juce::AffineTransform::scale (1.0f / line.px)
                                        .translated (line.snap (brain.getX()) - 0.5f / line.px, line.snap (brain.getY()) - 0.5f / line.px));
    paintScale (g, line);
    paintRegions (g, line);
}

void Specimen::paintScale (juce::Graphics& g, const Hairline& line)
{
    const float umPerPx = widthMicrons / brain.getWidth();
    const float left = brain.getX() - 10.0f, bottom = brain.getBottom() + 10.0f;

    // calibration: a tick every 50 um, longer every 250, on the left and bottom edges
    g.setColour (tone::grey (tone::dim));
    line.v (left, brain.getY(), bottom);
    line.h (left, brain.getRight(), bottom);
    for (int um = 0; (float) um <= widthMicrons; um += 50)
    {
        const float x = left + (float) um / umPerPx, len = um % 250 == 0 ? 6.0f : 3.0f;
        if (x > brain.getRight())
            break;
        line.v (x, bottom, bottom + len);
    }
    for (int um = 0; (float) um <= heightMicrons; um += 50)
    {
        const float y = bottom - (float) um / umPerPx, len = um % 250 == 0 ? 6.0f : 3.0f;
        if (y < brain.getY())
            break;
        line.h (left - len, left, y);
    }
    g.setFont (fonts->at (type::small));
    g.setColour (tone::grey (tone::mid));
    g.drawText ("0", juce::Rectangle<float> (left - 22.0f, bottom + 2.0f, 16.0f, 12.0f), juce::Justification::centredRight);

    // the scale bar: a 1-2-5 length near 80 px, picked again on every resize
    const float um = niceMicrons (80.0f * umPerPx), len = um / umPerPx;
    const float y = bottom + 58.0f;
    g.setColour (tone::grey (tone::bright));
    line.h (left, left + len, y);
    line.v (left, y - 3.0f, y + 3.0f);
    line.v (left + len, y - 3.0f, y + 3.0f);
    g.setFont (fonts->at (type::data));
    g.drawText (microns (um), juce::Rectangle<float> (left + len + 8.0f, y - 8.0f, 80.0f, 16.0f), juce::Justification::centredLeft);

    // the light's calibration: what each step of grey means
    {
        g.setFont (fonts->at (type::small));
        float x = left + len + 90.0f;
        auto swatch = [&] (float level, const juce::String& label) {
            g.setColour (tone::grey (level));
            g.fillRect (juce::Rectangle<float> (x, y - 3.0f, 6.0f, 6.0f));
            g.setColour (tone::grey (tone::mid));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label);
            g.drawText (label, juce::Rectangle<float> (x + 10.0f, y - 8.0f, w + 2.0f, 16.0f), juce::Justification::centredLeft);
            x += 10.0f + w + 14.0f;
        };
        swatch (tone::floor, "silent");
        swatch (tone::dim, "fired this session");
        swatch (levelFor (10.0f), "10 Hz");
        swatch (tone::live, utf8 ("\xe2\x89\xa5 100 Hz"));
    }

    // what the picture is, and what it holds
    g.setColour (tone::grey (tone::dim));
    g.setFont (fonts->at (type::small));
    const auto dot = utf8 ("  \xc2\xb7  ");
    const auto counts = grouped (flydata::neurons) + " neurons" + dot + grouped (flydata::synapses) + " connections" + dot + grouped (synapseCount()) + " synapses";
    const auto what = "frontal projection" + dot + "anchor points" + dot + microns (widthMicrons) + utf8 (" \xc3\x97 ") + microns (heightMicrons);
    // top left, as on a slide's label
    g.drawText (counts, juce::Rectangle<float> (14.0f, 8.0f, 600.0f, 16.0f), juce::Justification::centredLeft);
    g.drawText (what, juce::Rectangle<float> (14.0f, 24.0f, 600.0f, 16.0f), juce::Justification::centredLeft);
}

void Specimen::paintRegions (juce::Graphics& g, const Hairline& line)
{
    const auto font = fonts->at (type::small);
    g.setFont (font);
    struct Placed
    {
        juce::Point<float> at;
        float x, w;
        juce::String text;
    };
    for (const bool above : { true, false })
    {
        std::vector<Placed> row;
        for (const auto& r : regions)
        {
            if (r.above != above)
                continue;
            for (int k = 0; k < flydata::anchorCount; ++k)
                if (juce::String (flydata::anchors[k].name) == r.anchor)
                {
                    const juce::Point<float> at { brain.getX() + brain.getWidth() * flydata::anchors[k].x,
                                                  brain.getY() + brain.getHeight() * flydata::anchors[k].y };
                    const juce::String text (r.name);
                    const float w = juce::GlyphArrangement::getStringWidth (font, text);
                    row.push_back ({ at, at.x - 0.5f * w, w, text });
                }
        }
        std::sort (row.begin(), row.end(), [] (const Placed& a, const Placed& b) { return a.at.x < b.at.x; });

        // side by side in one row, never touching; below, where the regions crowd the midline,
        // the row is centred on them again so the leaders fan out both ways
        for (size_t k = 1; k < row.size(); ++k)
            row[k].x = std::max (row[k].x, row[k - 1].x + row[k - 1].w + 32.0f);
        if (! above && ! row.empty())
        {
            float anchorsMid = 0.0f;
            for (const auto& p : row)
                anchorsMid += p.at.x / (float) row.size();
            const float rowLeft = row.front().x, rowRight = row.back().x + row.back().w;
            float shift = anchorsMid - 0.5f * (rowLeft + rowRight);
            shift = juce::jlimit (brain.getX() - rowLeft, brain.getRight() - rowRight, shift);
            for (auto& p : row)
                p.x += shift;
        }

        const float textY = above ? brain.getY() - 26.0f : brain.getBottom() + 34.0f;
        const float bus = above ? textY + 12.0f : textY - 12.0f, stub = above ? textY + 7.0f : textY - 7.0f;
        for (size_t k = 0; k < row.size(); ++k)
        {
            const auto& p = row[k];
            g.setColour (tone::grey (tone::dim));
            g.drawRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre (p.at).expanded (0.5f * line.width()), line.width());
            // from the region to the bus, along it to the name, and a short stub up or down to it;
            // the middle names keep their stub between their neighbours' leaders so none cross
            float tx = juce::jlimit (p.x, p.x + p.w, p.at.x);
            if (k > 0 && k + 1 < row.size())
                tx = juce::jlimit (row[k - 1].at.x + 4.0f, row[k + 1].at.x - 4.0f, tx);
            if (above)
                line.v (p.at.x, bus, p.at.y - 3.0f);
            else
                line.v (p.at.x, p.at.y + 3.0f, bus);
            line.h (std::min (p.at.x, tx), std::max (p.at.x, tx) + line.width(), bus);
            line.v (tx, std::min (bus, stub), std::max (bus, stub));
            g.setColour (tone::grey (tone::mid));
            g.drawText (p.text, juce::Rectangle<float> (p.x, textY - 7.0f, p.w + 2.0f, 14.0f), juce::Justification::centredLeft);
        }
    }
}
} // namespace dendrite::ui
