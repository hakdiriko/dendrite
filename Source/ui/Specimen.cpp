#include "Specimen.h"
#include "rig/Recording.h"
#include "rig/Identities.h"
#include "rig/Depth.h"

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

    // two-photon read as dF/F (3.6): the floor at its own baseline (or below), white at +100%
    float deltaLevel (float dff) noexcept
    {
        return tone::floor + (tone::live - tone::floor) * std::sqrt (juce::jlimit (0.0f, 1.0f, dff));
    }

    juce::String microns (float um) { return juce::String (juce::roundToInt (um)) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb5m")); }

    // "0.5x", "4x", "3.2x"
    juce::String magnification (float z)
    {
        const bool whole = std::abs (z - std::round (z)) < 0.05f;
        return (whole ? juce::String (juce::roundToInt (z)) : juce::String (z, 1)) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97"));
    }

    // where FlyData's anchors are, and what the rig calls them
    struct Region
    {
        const char* anchor;
        const char* name;
        const char* shortName; // when the full names don't fit the window's width
        bool above;
        const char* term;      // its entry in the reference (3.6)
    };
    const Region regions[] = {
        { "eyeLeft", "LPLC2 left, looming", "LPLC2 left", true, "lplc2" },
        { "mushroomBody", "KC, mushroom body left", "KC left", true, "kc" },
        { "eyeRight", "LPLC2 right, looming", "LPLC2 right", true, "lplc2" },
        { "ears", "JO, johnston's organ", "JO", false, "jo" },
        { "giantFiber", "DNp01, giant fiber", "DNp01", false, "dnp01" },
        { "grooming", "DNg11, grooming", "DNg11", false, "dng11" },
    };

} // namespace

Specimen::Specimen (DendriteProcessor& p)
    : processor (p),
      // while the hand holds the light, the hand is the truth: a late echo from the host is ignored
      feedLink (*p.params.getParameter ("feed"), [this] (float v) { if (dragPart != Part::inside) feed = v; }),
      spaceLink (*p.params.getParameter ("space"), [this] (float v) { if (dragPart != Part::ring) space = v; }),
      throwLink (*p.params.getParameter ("throw"), [this] (float v) { throwOn = v > 0.5f; })
{
    setOpaque (true);

    // every neuron in microns about the brain's centre; the depth about the median neuron's, so the
    // a turn of the brain turns it about where its mass is
    place.resize ((size_t) neurons);
    proj.resize ((size_t) neurons);
    projUm.resize ((size_t) neurons * 2);
    std::vector<float> depths ((size_t) neurons);
    for (int n = 0; n < neurons; ++n)
        depths[(size_t) n] = (float) flydata::neuronZ[n] / 65535.0f * flydata::depthMicrons;
    auto middle = depths;
    std::nth_element (middle.begin(), middle.begin() + neurons / 2, middle.end());
    const float zc = middle[(size_t) (neurons / 2)];
    for (int n = 0; n < neurons; ++n)
        place[(size_t) n] = { ((float) flydata::neuronX[n] / 65535.0f - 0.5f) * widthMicrons,
                              ((float) flydata::neuronY[n] / 65535.0f - 0.5f) * heightMicrons,
                              depths[(size_t) n] - zc };
    {
        // the reach the dial's small brain is scaled to: 98% of the neurons lie within it
        std::vector<float> reach ((size_t) neurons);
        for (int n = 0; n < neurons; ++n)
        {
            const auto& q = place[(size_t) n];
            reach[(size_t) n] = std::sqrt (q.x * q.x + q.y * q.y + q.z * q.z);
        }
        std::nth_element (reach.begin(), reach.begin() + neurons * 98 / 100, reach.end());
        placeRadius = std::max (1.0f, reach[(size_t) (neurons * 98 / 100)]);
    }

    // the tracts: the cut's strongest connections by synapse count (inhibitory ones too), at most 24
    // from any one neuron so a few hubs don't take them all
    {
        std::vector<std::pair<int, uint32_t>> all;
        all.reserve (flydata::rowStart[neurons]);
        for (int n = 0; n < neurons; ++n)
            for (uint32_t k = flydata::rowStart[n]; k < flydata::rowStart[n + 1]; ++k)
                if (flydata::target[k] != n)
                    all.push_back ({ std::abs ((int) flydata::weight[k]), k });
        std::sort (all.begin(), all.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<uint8_t> taken ((size_t) neurons, 0);
        std::vector<int> fromOf (all.empty() ? 0 : (size_t) flydata::rowStart[neurons]);
        for (int n = 0; n < neurons; ++n)
            for (uint32_t k = flydata::rowStart[n]; k < flydata::rowStart[n + 1]; ++k)
                fromOf[k] = n;
        const float strongest = all.empty() ? 1.0f : (float) std::log1p ((double) all.front().first);
        for (const auto& [w, k] : all)
        {
            if ((int) tracts.size() >= tractCount)
                break;
            const int from = fromOf[k];
            if (taken[(size_t) from] >= 24)
                continue;
            ++taken[(size_t) from];
            tracts.push_back ({ (uint16_t) from, (uint16_t) flydata::target[k], (float) std::log1p ((double) w) / strongest });
        }
    }

    // the atlas's sites: the fly's own populations, each with the numbers it runs
    using P = rig::Recording;
    auto members = [&] (std::initializer_list<int> pops) {
        std::vector<uint16_t> out;
        for (int pop : pops)
            for (auto n : processor.recording.population (pop))
                out.push_back (n);
        return out;
    };
    sites.push_back ({ "eyes (LPLC2)", members ({ P::eyesLeft }), { 6, 7 } });
    sites.push_back ({ "eyes (LPLC2)", members ({ P::eyesRight }), { 6, 7 } });
    sites.push_back ({ "mushroom body", members ({ P::kenyon }), { 0, 1 } });
    sites.push_back ({ "giant fiber (DNp01)", members ({ P::giantFiber }), { 2, 3 } });
    sites.push_back ({ "grooming (DNg11)", members ({ P::groom }), { 4, -1 } });
    sites.push_back ({ "steering (DNa01/02)", members ({ P::steerLeft, P::steerRight }), { 5, -1 } });
    for (size_t k = 0; k < taste.size(); ++k)
        taste[k] = p.params.getParameter (DendriteProcessor::tasteParams[k].id);

    feedLink.sendInitialUpdate();
    spaceLink.sendInitialUpdate();
    throwLink.sendInitialUpdate();
}

Specimen::~Specimen()
{
    if (touching)
        setThrow (false); // never leave a throw held when the window closes mid-press
}

void Specimen::resized()
{
    // room for the region names above and below, the ticks and scale on the left and bottom
    auto r = getLocalBounds().toFloat();
    // 3.7, lights out: the margins for names, ticks and the rail close as the bench fades, so the
    // brain grows to the whole window
    if (bare > 0.0f)
    {
        const float keep = 1.0f - bare, edge = 28.0f;
        const auto all = r;
        r.removeFromLeft (edge + keep * (52.0f - edge));
        r.removeFromRight (edge + keep * (198.0f - edge));
        r.removeFromTop (edge + keep * (72.0f - edge));
        r.removeFromBottom (edge + keep * (84.0f - edge));
        railShown = bare < 0.5f;
        // the rail stays where it was while it fades, and is nothing to the hand once it has gone
        rail = railShown ? all.withTrimmedLeft (all.getWidth() - 174.0f).withTrimmedRight (24.0f).withTrimmedTop (44.0f).withTrimmedBottom (10.0f)
                         : juce::Rectangle<float> (all.getRight() + 24.0f, 44.0f, 0.0f, 0.0f);
        {
            auto c = rail;
            c.removeFromTop (20.0f);
            methodList = railShown ? c.removeFromTop (railRow * (float) rig::Recording::methods) : juce::Rectangle<float>();
            c.removeFromTop (28.0f);
            insetArea = railShown ? c.removeFromTop (std::round (c.getWidth() * heightMicrons / widthMicrons)) : juce::Rectangle<float>();
        }
        const float aspect = widthMicrons / heightMicrons;
        const float w = std::min (r.getWidth(), r.getHeight() * aspect);
        brain = juce::Rectangle<float> (w, w / aspect).withCentre (r.getCentre());
        return;
    }
    // with the reference open (3.6) the brain moves right of it; in a small window the rail steps
    // aside while the entry reads, so the brain keeps a useful size (closing the entry brings it back)
    r.removeFromLeft (referenceWidth > 0 ? 16.0f + (float) referenceWidth : 52.0f);
    r.removeFromRight (24.0f);
    railShown = referenceWidth == 0 || r.getWidth() - 174.0f >= 380.0f;
    // the rail: the imaging method, the 2-DG brain, the view
    if (railShown)
    {
        rail = r.removeFromRight (150.0f).withTrimmedTop (44.0f).withTrimmedBottom (10.0f); // under the caption; the legend stops short of it
        r.removeFromRight (24.0f);
    }
    else
        rail = juce::Rectangle<float> (r.getRight() + 24.0f, 44.0f, 0.0f, 0.0f); // nothing to hit; its left edge still bounds the labels
    {
        auto c = rail;
        c.removeFromTop (20.0f); // its title
        methodList = c.removeFromTop (railRow * (float) rig::Recording::methods);
        c.removeFromTop (28.0f);
        insetArea = c.removeFromTop (std::round (c.getWidth() * heightMicrons / widthMicrons));
    }
    r.removeFromTop (72.0f);
    r.removeFromBottom (84.0f);
    const float aspect = widthMicrons / heightMicrons;
    const float w = std::min (r.getWidth(), r.getHeight() * aspect);
    brain = juce::Rectangle<float> (w, w / aspect).withCentre (r.getCentre());
}

void Specimen::project()
{
    adoptField();
    yawNow = processor.recording.yaw.load();
    pitchNow = processor.recording.pitch.load();
    const bool turned = ! frontal();
    const float a = juce::degreesToRadians (yawNow), b = juce::degreesToRadians (pitchNow);
    const float ca = std::cos (a), sa = std::sin (a), cb = std::cos (b), sb = std::sin (b);
    projDepth.resize ((size_t) neurons);
    for (size_t n = 0; n < (size_t) neurons; ++n)
    {
        // turned about the vertical axis (yaw), then tilted about the horizontal (pitch); seen from the front
        const auto& p = place[n];
        const float x = p.x * ca + p.z * sa, z = -p.x * sa + p.z * ca;
        const float y = p.y * cb - z * sb;
        projUm[2 * n] = x + 0.5f * widthMicrons;
        projUm[2 * n + 1] = y + 0.5f * heightMicrons;
        proj[n] = toScreen (projUm[2 * n], projUm[2 * n + 1]);
        // how far back it now lies (0 nearest), so the far side of a turned brain reads as far
        projDepth[n] = turned ? juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * (p.y * sb + z * cb) / placeRadius) : 0.0f;
    }
}

//==============================================================================
// the magnification: how much of the plate the window sees, and where

namespace
{
    // the field's centre on one axis, so the field never leaves the box (smaller than the box: centred)
    float fieldCentre (float c, float size, float z) noexcept
    {
        const float half = 0.5f * size / z;
        return half >= 0.5f * size ? 0.5f * size : juce::jlimit (half, size - half, c);
    }
} // namespace

void Specimen::clampField()
{
    zoomCx = fieldCentre (zoomCx, widthMicrons, zoomNow);
    zoomCy = fieldCentre (zoomCy, heightMicrons, zoomNow);
}

void Specimen::storeField()
{
    auto& rec = processor.recording;
    seenZoom = zoomNow;
    seenX = zoomCx / widthMicrons;
    seenY = zoomCy / heightMicrons;
    rec.zoom = seenZoom;
    rec.zoomX = seenX;
    rec.zoomY = seenY;
}

void Specimen::adoptField()
{
    const auto& rec = processor.recording;
    const float z = rec.zoom.load(), x = rec.zoomX.load(), y = rec.zoomY.load();
    if (z == seenZoom && x == seenX && y == seenY)
        return;
    zoomNow = zoomGoal = juce::jlimit (rig::Recording::minZoom, rig::Recording::maxZoom, z);
    zoomCx = x * widthMicrons;
    zoomCy = y * heightMicrons;
    easing = false;
    clampField();
    storeField();
}

void Specimen::setZoom (float z, juce::Point<float> keepAt)
{
    const auto u = toMicrons (keepAt);
    zoomNow = zoomGoal = juce::jlimit (rig::Recording::minZoom, rig::Recording::maxZoom, z);
    easing = false;
    const float s = pixelsPerMicron();
    zoomCx = u.x - (keepAt.x - brain.getCentreX()) / s;
    zoomCy = u.y - (keepAt.y - brain.getCentreY()) / s;
    clampField();
    storeField();
}

void Specimen::viewHome()
{
    // frontal and whole again, eased: the brain swings back
    zoomGoal = 1.0f;
    goalCx = 0.5f * widthMicrons;
    goalCy = 0.5f * heightMicrons;
    easing = true;
    spinYaw = spinPitch = 0.0f;
    processor.recording.yaw = 0.0f;
    processor.recording.pitch = 0.0f;
}

void Specimen::turnBy (float dYaw, float dPitch)
{
    auto& rec = processor.recording;
    float yaw = rec.yaw.load() + dYaw;
    yaw -= 360.0f * std::round (yaw / 360.0f);
    const float pitch = rec.pitch.load() + dPitch;
    if (std::abs (pitch) > 90.0f)
        spinPitch = 0.0f; // the stage stops at a quarter turn up or down
    rec.yaw = yaw;
    rec.pitch = juce::jlimit (-90.0f, 90.0f, pitch);
}

namespace
{
    // a fixed scatter for photon k of neuron n, within a disc of radius 1 (a hash, not a random draw:
    // the grain stays put while the count holds)
    juce::Point<float> grain (int n, int k) noexcept
    {
        uint32_t h = (uint32_t) n * 2654435761u ^ ((uint32_t) k + 0x9e3779b9u) * 2246822519u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        const float r = std::sqrt ((float) (h & 0xffff) / 65535.0f), t = (float) (h >> 16) / 65535.0f * juce::MathConstants<float>::twoPi;
        return { r * std::cos (t), r * std::sin (t) };
    }
} // namespace

void Specimen::renderPoints (float px)
{
    renderMethod (processor.recording.method.load(), px);
}

void Specimen::renderMethod (int method, float px)
{
    const int w = std::max (1, juce::roundToInt (brain.getWidth() * px)), h = std::max (1, juce::roundToInt (brain.getHeight() * px));
    if (points.getWidth() != w || points.getHeight() != h)
        points = juce::Image (juce::Image::RGB, w, h, true);
    const auto& rec = processor.recording;
    using M = rig::Recording::Method;

    // one logical pixel at the default window (816 um across ~790 px: about 1 um a pixel), growing
    // with the window so a large picture keeps its density; always whole physical pixels
    // magnified a neuron grows with the square root of the magnification (a soma is a few um across)
    const int dot = std::max (1, juce::roundToInt (px * std::max (1.0f, brain.getWidth() / 790.0f) * std::sqrt (std::max (1.0f, zoomNow))));

    if (method == M::deoxyglucose)
    {
        double at = 0.0, lived = 0.0;
        const float* frame = lapsePicture (at, lived);
        renderUptake (points, frame, lived, false, 1.0f / (pixelsPerMicron() * px), toMicrons (brain.getTopLeft()), dot);
        if (! noElectrode)
        {
            juce::Image::BitmapData bits (points, juce::Image::BitmapData::readWrite);
            lightTissue (bits, w, h, px, dot);
        }
        return;
    }

    const float most = (float) std::log1p ((double) std::max<uint32_t> (1u, rec.mostExposed()));

    juce::Image::BitmapData bits (points, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y)
        std::memset (bits.getLinePointer (y), 0, (size_t) (bits.pixelStride * w));
    auto at = [&] (int n, int& x0, int& y0) {
        x0 = (int) std::floor ((proj[(size_t) n].x - brain.getX()) * px);
        y0 = (int) std::floor ((proj[(size_t) n].y - brain.getY()) * px);
        return x0 >= 0 && y0 >= 0 && x0 <= w - dot && y0 <= h - dot;
    };
    auto put = [&] (int x0, int y0, juce::uint8 v, int size, bool darker) {
        for (int y = y0; y < y0 + size; ++y)
            for (int x = x0; x < x0 + size; ++x)
            {
                if (x < 0 || y < 0 || x >= w || y >= h)
                    continue;
                auto* p = bits.getPixelPointer (x, y);
                if (darker ? p[0] > v || p[0] == 0 : p[0] < v) // where two neurons share a pixel, the brighter shows (voltage: the darker)
                    p[0] = p[1] = p[2] = v;
            }
    };
    auto grey = [] (float level) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * level)); };
    // turned, the far side dims toward the floor (depth cueing): the nearer neuron wins a shared pixel
    auto cue = [&] (int n, float level) { return tone::floor + (level - tone::floor) * (1.0f - 0.6f * projDepth[(size_t) n]); };
    for (int n = 0; n < neurons; ++n)
    {
        int x0, y0;
        if (! at (n, x0, y0))
            continue;
        const auto e = rec.exposure (n);
        const float exposed = e > 0 ? (tone::dim - tone::floor) * (float) std::log1p ((double) e) / most : 0.0f;
        switch (method)
        {
            case M::twoPhoton:
            {
                // GCaMP6s read as dF/F (3.6): each neuron against its own last 20 s, so what fires
                // steadily sinks to the floor and only a change lights; white at +100%
                put (x0, y0, grey (cue (n, deltaLevel (rec.deltaF (n)))), dot, false);
                break;
            }
            case M::voltage:
            {
                // ASAP2f dims when the membrane fires: every labelled neuron glows at rest, a spike in this
                // frame darkens it (two spikes or more: to black)
                const int k = rec.frameSpikes (n);
                put (x0, y0, grey (k == 0 ? tone::mid : k == 1 ? tone::floor : 0.04f), dot, true);
                break;
            }
            case M::photonCounting:
            {
                // one photon a spike, over the last second, each a grain within 3 um of its neuron
                const int k = std::min (rec.photons (n), 32);
                const float spread = 3.0f * pixelsPerMicron() * px;
                for (int j = 0; j < k; ++j)
                {
                    const auto g = grain (n, j);
                    const int gx = x0 + juce::roundToInt (g.x * spread), gy = y0 + juce::roundToInt (g.y * spread);
                    if (gx < 0 || gy < 0 || gx >= w || gy >= h)
                        continue;
                    auto* p = bits.getPixelPointer (gx, gy);
                    p[0] = p[1] = p[2] = (juce::uint8) std::min (255, p[0] + (p[0] == 0 ? 150 : 60));
                }
                break;
            }
            case M::tracts:
                // the neurons only as a faint ground; the tracts are laid over them (renderTracts)
                put (x0, y0, grey (cue (n, tone::floor * 0.8f + exposed * 0.3f)), dot, false);
                break;
            default:
            {
                // the trace: the floor, raised by this session's exposure (to the dim step), lit by firing now
                float level = tone::floor + exposed;
                const float hz = rec.trace (n) / (float) rig::Recording::traceSeconds;
                if (hz >= 1.0f)
                    level = std::max (level, tone::rate (hz));
                put (x0, y0, grey (cue (n, level)), dot, false);
                break;
            }
        }
    }

    if (method == M::tracts)
        renderTracts (bits, w, h, px);
    // the light on the tissue (not on voltage: there a neuron darkens when it fires)
    if (! noElectrode && method != M::voltage)
        lightTissue (bits, w, h, px, dot);

    // scars, burned in over the neurons: a cross at each, never removed (on the trace and the tracts)
    if (method != M::traceMethod && method != M::tracts)
        return;
    const int arm = 2 * dot;
    for (int n = 0; n < neurons; ++n)
    {
        const int count = rec.scars (n);
        int cx, cy;
        if (count == 0 || ! at (n, cx, cy))
            continue;
        const auto v = grey (tone::scar (rec.scarAge (n), count));
        for (int t = 0; t < dot; ++t)
            for (int k = -arm; k < dot + arm; ++k)
            {
                put (cx + k, cy + t, v, 1, false); // across
                put (cx + t, cy + k, v, 1, false); // down
            }
    }
}

void Specimen::renderUptake (juce::Image& into, const float* uptake, double lived, bool front, float umPerPixel, juce::Point<float> origin, int dotPx)
{
    // the film (3.6): crisp, each neuron where it lies. Any uptake greys it (on a log scale against
    // the session's most); uptake beyond what ordinary music costs that neuron in the same time
    // lights it, so the bright part is what this session did that other music would not
    const auto& rec = processor.recording;
    const int w = into.getWidth(), h = into.getHeight();
    float mostAbove = 1.0f;
    for (int n = 0; n < neurons; ++n)
        mostAbove = std::max (mostAbove, rec.uptakeAbove (n));
    const float logMost = (float) std::log1p (std::max (1.0f, rec.mostUptake())), logAbove = (float) std::log1p (mostAbove);
    juce::Image::BitmapData bits (into, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y)
        std::memset (bits.getLinePointer (y), 0, (size_t) (bits.pixelStride * w));
    for (int n = 0; n < neurons; ++n)
    {
        const float u = uptake != nullptr ? uptake[n] : 0.0f;
        if (u <= 0.0f)
            continue;
        const float ux = front ? (float) flydata::neuronX[n] / 65535.0f * widthMicrons : projUm[(size_t) (2 * n)];
        const float uy = front ? (float) flydata::neuronY[n] / 65535.0f * heightMicrons : projUm[(size_t) (2 * n + 1)];
        const int x = (int) std::floor ((ux - origin.x) / umPerPixel), y = (int) std::floor ((uy - origin.y) / umPerPixel);
        if (x < 0 || y < 0 || x >= w || y >= h)
            continue;
        const float above = rec.aboveOf (u, n, lived);
        float level = tone::floor * 0.7f + (tone::dim - tone::floor * 0.7f) * (float) std::log1p (u) / logMost;
        if (above > 0.0f)
        {
            // how many times what ordinary music costs it: mid grey at twice, white at 64 times
            const float times = u / std::max (1.0e-3f, rec.expectedOf (n, lived));
            level = std::max (level, tone::mid + (tone::live - tone::mid) * std::pow (juce::jlimit (0.0f, 1.0f, std::log2 (times / 2.0f) / 5.0f), 0.7f));
        }
        const auto v = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * level));
        // the brighter wins where two share a pixel; the brightest grow by one pixel, as dense film spreads
        const int size = dotPx + (above > 0.25f * mostAbove && dotPx > 1 ? 1 : 0);
        for (int yy = y; yy < std::min (h, y + size); ++yy)
            for (int xx = x; xx < std::min (w, x + size); ++xx)
            {
                auto* p = bits.getPixelPointer (xx, yy);
                if (p[0] < v)
                    p[0] = p[1] = p[2] = v;
            }
    }
}

juce::String Specimen::topTypes (const float* uptake, double lived, int count) const
{
    // the cell types this session drove beyond ordinary music, most first (each type's total excess)
    namespace id = rig::identities;
    std::unordered_map<int, float> byType;
    for (int n = 0; n < neurons; ++n)
        if (uptake != nullptr && uptake[n] > 0.0f)
            if (const float a = processor.recording.aboveOf (uptake[n], n, lived); a > 0.0f)
                byType[(int) id::type[n]] += a;
    std::vector<std::pair<float, int>> ranked;
    for (const auto& [t, a] : byType)
        if (juce::String (id::typeName[t]).isNotEmpty())
            ranked.push_back ({ a, t });
    std::sort (ranked.begin(), ranked.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    juce::StringArray names;
    for (int k = 0; k < std::min (count, (int) ranked.size()); ++k)
        names.add (id::typeName[ranked[(size_t) k].second]);
    return names.joinIntoString (", ");
}

const float* Specimen::lapsePicture (double& atSeconds, double& lived)
{
    // the time-lapse plays the session's keyframes over lapsePlaySeconds, then holds on now
    const auto& rec = processor.recording;
    const int frames = rec.lapseFrames();
    atSeconds = rec.sessionSeconds();
    lived = rec.steppedSeconds();
    if (frames == 0 || lapseStarted < 0.0 || clockNow - lapseStarted >= lapsePlaySeconds)
    {
        static thread_local std::vector<float> live;
        live.resize ((size_t) neurons);
        for (int n = 0; n < neurons; ++n)
            live[(size_t) n] = rec.uptake (n);
        return live.data();
    }
    const double t = (clockNow - lapseStarted) / lapsePlaySeconds * rec.lapseTime (frames - 1);
    int k = 0;
    while (k + 1 < frames && rec.lapseTime (k + 1) <= t)
        ++k;
    atSeconds = t;
    lived = rec.lapseStepped (k);
    return rec.lapseFrame (k);
}

namespace
{
    // the value below which a fraction q of the nonzero pixels lie (a film's exposure, set by its densest part)
    float percentile (const std::vector<float>& v, float q)
    {
        std::vector<float> lit;
        lit.reserve (v.size() / 4);
        for (float x : v)
            if (x > 0.0f)
                lit.push_back (x);
        if (lit.empty())
            return 1.0f;
        auto at = lit.begin() + (long) ((float) (lit.size() - 1) * q);
        std::nth_element (lit.begin(), at, lit.end());
        return std::max (1.0e-6f, *at);
    }
} // namespace

void Specimen::renderTracts (juce::Image::BitmapData& bits, int w, int h, float px)
{
    // each tract a gentle curve from its neuron to its target, bowed toward the brain's middle (the
    // curves are drawn; the links are real). The skeleton: every tract faintly, by its synapses,
    // drawn again only when the view changes. The light: each tract its sender's spikes over the
    // last second. Both add up where tracts cross, as fibre bundles do, and are toned like film.
    const size_t size = (size_t) (w * h);
    const std::array<float, 7> view { yawNow, pitchNow, zoomNow, zoomCx, zoomCy, (float) w, (float) h };
    const auto& rec = processor.recording;
    const float ox = brain.getX(), oy = brain.getY();
    const auto mid = toScreen (0.5f * widthMicrons, 0.5f * heightMicrons);
    auto splat = [&] (std::vector<float>& into, const Tract& t, float amount) {
        const auto a = proj[t.from], b = proj[t.to];
        const float ax = (a.x - ox) * px, ay = (a.y - oy) * px, bx = (b.x - ox) * px, by = (b.y - oy) * px;
        const float mx = 0.5f * (ax + bx), my = 0.5f * (ay + by);
        const float cx = mx + ((mid.x - ox) * px - mx) * 0.12f, cy = my + ((mid.y - oy) * px - my) * 0.12f;
        const float len = std::abs (bx - ax) + std::abs (by - ay);
        const int steps = juce::jlimit (4, 600, (int) len);
        for (int i = 0; i <= steps; ++i)
        {
            const float u = (float) i / (float) steps, v = 1.0f - u;
            const float x = v * v * ax + 2.0f * u * v * cx + u * u * bx, y = v * v * ay + 2.0f * u * v * cy + u * u * by;
            const int x0 = (int) std::floor (x), y0 = (int) std::floor (y);
            if (x0 < 0 || y0 < 0 || x0 >= w - 1 || y0 >= h - 1)
                continue;
            const float fx = x - (float) x0, fy = y - (float) y0;
            float* p = into.data() + (size_t) (y0 * w + x0);
            p[0] += amount * (1.0f - fx) * (1.0f - fy);
            p[1] += amount * fx * (1.0f - fy);
            p[w] += amount * (1.0f - fx) * fy;
            p[w + 1] += amount * fx * fy;
        }
    };
    if (view != skeletonFor || skeleton.size() != size)
    {
        skeletonFor = view;
        skeleton.assign (size, 0.0f);
        for (const auto& t : tracts)
            splat (skeleton, t, (0.2f + 0.8f * t.strength * t.strength) / px);
        skeletonScale = percentile (skeleton, 0.995f);
    }
    // the light, as the demo drew it: the busiest connections now (synapses x the sender's spikes
    // in the last second) out of all 1.76 million, picked again five times a second
    if (clockNow - busyAt > 0.2 || clockNow < busyAt)
    {
        busyAt = clockNow;
        busy.clear();
        for (int n = 0; n < neurons; ++n)
            if (const int k = rec.photons (n); k > 0)
                for (uint32_t e = flydata::rowStart[n]; e < flydata::rowStart[n + 1]; ++e)
                    if (flydata::target[e] != n)
                        busy.push_back ({ (uint16_t) n, flydata::target[e], (float) std::abs ((int) flydata::weight[e]) * (float) k });
        if (busy.size() > (size_t) busyCount)
        {
            std::nth_element (busy.begin(), busy.begin() + busyCount, busy.end(), [] (const Tract& a, const Tract& b) { return a.strength > b.strength; });
            busy.resize ((size_t) busyCount);
        }
    }
    tractLight.assign (size, 0.0f);
    float most = 1.0f;
    for (const auto& t : busy)
        most = std::max (most, t.strength);
    for (const auto& t : busy)
        splat (tractLight, t, std::sqrt (t.strength / most) / px);
    // each layer against its own densest pixels, so the film shows structure, never one white sheet
    const float lightScale = busy.empty() ? 1.0f : percentile (tractLight, 0.995f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const size_t i = (size_t) (y * w + x);
            // film: the skeleton never past the mid grey, the light up to white
            const float level = tone::floor * 1.4f * std::sqrt (std::min (1.0f, skeleton[i] / skeletonScale))
                              + tone::live * std::pow (std::min (1.0f, tractLight[i] / lightScale), 0.7f);
            const auto v = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * std::min (1.0f, level)));
            auto* p = bits.getPixelPointer (x, y);
            if (p[0] < v)
                p[0] = p[1] = p[2] = v;
        }
}

void Specimen::lightTissue (juce::Image::BitmapData& bits, int w, int h, float px, int dot)
{
    // the 590 nm spot as it falls on the tissue: the neurons under it brighten with the beam's
    // gaussian profile (half its radius wide), by its power; a flash bleaches them white. This is
    // the light, not their firing: what they fire shows in the picture itself.
    const auto& rec = processor.recording;
    const float lx = rec.lightX.load() * widthMicrons, ly = rec.lightY.load() * heightMicrons;
    const float r = spotMicrons (space), sigma = 0.5f * r;
    const bool flash = throwOn || touching;
    const float power = flash ? 1.0f : 0.18f + 0.42f * feed;
    for (int n = 0; n < neurons; ++n)
    {
        const float dx = projUm[(size_t) (2 * n)] - lx, dy = projUm[(size_t) (2 * n + 1)] - ly;
        const float d2 = dx * dx + dy * dy;
        if (d2 > 4.0f * r * r)
            continue;
        const float beam = power * std::exp (-d2 / (2.0f * sigma * sigma));
        const auto v = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * (tone::floor + (tone::live - tone::floor) * beam)));
        const int x0 = (int) std::floor ((proj[(size_t) n].x - brain.getX()) * px), y0 = (int) std::floor ((proj[(size_t) n].y - brain.getY()) * px);
        for (int y = y0; y < y0 + dot; ++y)
            for (int x = x0; x < x0 + dot; ++x)
            {
                if (x < 0 || y < 0 || x >= w || y >= h)
                    continue;
                auto* p = bits.getPixelPointer (x, y);
                if (p[0] < v)
                    p[0] = p[1] = p[2] = v;
            }
    }
}

void Specimen::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    project();
    renderPoints (line.px);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed (points, juce::AffineTransform::scale (1.0f / line.px)
                                        .translated (line.snap (brain.getX()) - 0.5f / line.px, line.snap (brain.getY()) - 0.5f / line.px));
    hovered = findHover();
    // 3.7: the bench's words and numbers on the plate fade with it; bare, none are drawn at all
    const float chrome = 1.0f - bare;
    auto bench = [&] (auto&& draw) {
        if (chrome <= 0.001f)
            return;
        if (chrome < 0.999f)
            g.beginTransparencyLayer (chrome);
        draw();
        if (chrome < 0.999f)
            g.endTransparencyLayer();
    };
    bench ([&] { paintScale (g, line); });
    labels.clear();
    if (chrome <= 0.001f)
    {
        tagNeuron = -1;
        tagArea = {};
    }
    if (frontal() && std::abs (zoomNow - 1.0f) < 0.01f)
    {
        bench ([&] { paintRegions (g, line); });
        paintPath (g, line); // the anchors are placed on the unmagnified plate
    }
    measureLight();
    bench ([&] {
        paintSites (g, line);
        paintFocus (g, line);
    });
    if (! noElectrode)
        paintLight (g, line);
    bench ([&] {
        paintHover (g, line);
        if (railShown)
            paintRail (g, line);
    });
}

void Specimen::setBare (float amount)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    if (std::abs (amount - bare) < 1.0e-4f)
        return;
    bare = amount;
    if (bare > 0.5f)
    {
        hoverPart = Part::none;
        tagNeuron = -1;
        tagArea = {};
    }
    resized();
    repaint();
}

void Specimen::setReferenceWidth (int w)
{
    if (w == referenceWidth)
        return;
    referenceWidth = w;
    resized();
    repaint();
}

void Specimen::paintFocus (juce::Graphics& g, const Hairline& line)
{
    // 3.6: the focus's neurons ringed (or, when many, marked) and named; the rest steps back
    if (focus == nullptr || ! focus->active() || focus->neurons.empty())
        return;
    const auto& rec = processor.recording;
    {
        juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (brain.expanded (6.0f).toNearestInt());
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (brain.expanded (6.0f));
        const bool many = focus->neurons.size() > 120;
        juce::Point<float> c;
        int inside = 0;
        for (auto n : focus->neurons)
        {
            const auto at = proj[(size_t) n];
            const float hz = rec.trace (n) / (float) rig::Recording::traceSeconds;
            g.setColour (tone::grey (std::max (tone::mid, tone::rate (hz))));
            if (many)
                g.fillRect (juce::Rectangle<float> (2.0f, 2.0f).withCentre (at));
            else
                g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (at), line.width());
            if (brain.contains (at))
            {
                c += at;
                ++inside;
            }
        }
        if (inside == 0)
            return;
        c /= (float) inside;
        float hz = 0.0f;
        for (auto n : focus->neurons)
            hz += rec.trace (n) / (float) rig::Recording::traceSeconds;
        hz /= (float) focus->neurons.size();
        const auto text = focus->title + utf8 ("  \xc2\xb7  ") + juce::String ((int) focus->neurons.size()) + utf8 ("  \xc2\xb7  ") + juce::String (hz, 1) + " Hz";
        const auto small = fonts->at (type::small);
        const float w = juce::GlyphArrangement::getStringWidth (small, text) + 10.0f;
        auto box = juce::Rectangle<float> (c.x + 14.0f, c.y - 26.0f, w, 15.0f);
        if (box.getRight() > brain.getRight())
            box.setX (c.x - 14.0f - w);
        box.setX (std::max (brain.getX() - 4.0f, std::min (box.getX(), brain.getRight() + 4.0f - w)));
        box.setY (juce::jlimit (brain.getY(), brain.getBottom() - 15.0f, box.getY()));
        g.restoreState(); // the name may sit past the brain's edge: never cut it
        g.saveState();
        g.setColour (juce::Colours::black.withAlpha (0.9f));
        g.fillRect (box);
        g.setColour (tone::grey (tone::bright));
        g.drawRect (box, line.width());
        g.setFont (small);
        g.setColour (tone::grey (tone::live));
        g.drawText (text, box.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
    }
}

int Specimen::findHover() const
{
    if (tagNeuron >= 0 && tagArea.contains (hoverAt))
        return tagNeuron;
    if (dragPart != Part::none || touching || turning || hoverPart != Part::none || ! brain.expanded (6.0f).contains (hoverAt))
        return -1;
    int nearest = -1;
    float best = 8.0f * 8.0f;
    for (int n = 0; n < neurons; ++n)
    {
        const float d = neuronPoint (n).getDistanceSquaredFrom (hoverAt);
        if (d < best)
        {
            best = d;
            nearest = n;
        }
    }
    return nearest;
}

void Specimen::paintHover (juce::Graphics& g, const Hairline& line)
{
    // the neuron under the hand: who it is in its atlas, and its rate now
    const int nearest = hovered;
    if (nearest < 0)
    {
        tagArea = {};
        tagNeuron = -1;
        return;
    }
    // how crowded it is there: the neurons within 5 um of it
    const auto at = neuronPoint (nearest);
    const float r5 = 5.0f * pixelsPerMicron();
    int near = 0;
    for (int n = 0; n < neurons; ++n)
        if (neuronPoint (n).getDistanceSquaredFrom (at) <= r5 * r5)
            ++near;

    namespace id = rig::identities;
    g.setColour (tone::grey (tone::live));
    g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ line.snap (at.x), line.snap (at.y) }), line.width());
    const auto dot = utf8 ("  \xc2\xb7  ");
    const juce::String type (id::typeName[id::type[nearest]]);
    juce::String text = juce::String ((juce::int64) id::rootId[nearest]) + dot + (type.isEmpty() ? juce::String ("untyped") : type);
    if (const juce::String nt (id::transmitterName[id::transmitter[nearest]]); nt.isNotEmpty())
        text << dot << nt;
    if (const juce::String c (id::className[id::superClass[nearest]]); c.isNotEmpty())
        text << dot << c.replaceCharacter ('_', ' ');
    if (const juce::String s (id::sideName[id::side[nearest]]); s.isNotEmpty())
        text << dot << s;
    text << dot << juce::String (processor.recording.trace (nearest) / (float) rig::Recording::traceSeconds, 1) << " Hz";
    if (near > 1)
        text << dot << near << " within 5" << utf8 (" \xc2\xb5m");
    const int scarred = processor.recording.scars (nearest);
    if (scarred > 0)
        text << dot << scarred << (scarred == 1 ? " scar" : " scars");
    // written in the caption's second line, in place of what the picture is
    g.setColour (tone::grey (tone::bright));
    g.setFont (fonts->at (type::small));
    g.drawText (text, juce::Rectangle<float> (14.0f + (float) referenceWidth, 24.0f, (float) getWidth() - 28.0f - (float) referenceWidth, 16.0f), juce::Justification::centredLeft, true);

    // 3.6: its tag beside it, a link to its type's entry in the reference
    if (type.isNotEmpty())
    {
        const auto tag = type + utf8 (" \xe2\x80\xba");
        const float w = juce::GlyphArrangement::getStringWidth (fonts->at (type::small), tag) + 10.0f;
        auto box = juce::Rectangle<float> (at.x + 9.0f, at.y - 21.0f, w, 15.0f);
        if (box.getRight() > rail.getX() - 8.0f)
            box.setX (at.x - 9.0f - w);
        const bool over = box.contains (hoverAt);
        g.setColour (juce::Colours::black.withAlpha (0.9f));
        g.fillRect (box);
        g.setColour (tone::grey (over ? tone::live : tone::dim));
        g.drawRect (box, line.width());
        g.setColour (tone::grey (over ? tone::live : tone::bright));
        g.drawText (tag, box.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
        tagArea = box;
        tagNeuron = nearest;
    }
    else
    {
        tagArea = {};
        tagNeuron = -1;
    }
}

void Specimen::paintScale (juce::Graphics& g, const Hairline& line)
{
    const float umPerPx = 1.0f / pixelsPerMicron();
    const float left = brain.getX() - 10.0f, bottom = brain.getBottom() + 10.0f;

    // calibration: the field's microns from the box's left and bottom edges, a tick every 1-2-5 step
    // near 50 px (50 um at 1x), longer every fifth; the field's corner is labelled
    const auto low = toMicrons (brain.getBottomLeft()), high = toMicrons (brain.getTopRight());
    const float u0 = std::max (0.0f, low.x), u1 = std::min (widthMicrons, high.x);
    const float v0 = std::max (0.0f, heightMicrons - low.y), v1 = std::min (heightMicrons, heightMicrons - high.y);
    const float step = niceMicrons (50.0f * umPerPx);
    g.setColour (tone::grey (tone::dim));
    line.v (left, brain.getY(), bottom);
    line.h (left, brain.getRight(), bottom);
    for (float um = std::ceil (u0 / step - 1.0e-3f) * step; um <= u1 + 1.0e-3f; um += step)
    {
        const bool major = juce::roundToInt (um / step) % 5 == 0;
        line.v (toScreen (um, 0.0f).x, bottom, bottom + (major ? 6.0f : 3.0f));
    }
    for (float um = std::ceil (v0 / step - 1.0e-3f) * step; um <= v1 + 1.0e-3f; um += step)
    {
        const bool major = juce::roundToInt (um / step) % 5 == 0;
        const float len = major ? 6.0f : 3.0f;
        line.h (left - len, left, toScreen (0.0f, heightMicrons - um).y);
    }
    g.setFont (fonts->at (type::small));
    g.setColour (tone::grey (tone::mid));
    if (juce::roundToInt (u0) == 0 && juce::roundToInt (v0) == 0)
        g.drawText ("0", juce::Rectangle<float> (left - 22.0f, bottom + 2.0f, 16.0f, 12.0f), juce::Justification::centredRight);
    else
    {
        // where the field starts: across under the corner, up beside it
        g.drawText (juce::String (juce::roundToInt (u0)), juce::Rectangle<float> (left - 2.0f, bottom + 8.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText (juce::String (juce::roundToInt (v0)), juce::Rectangle<float> (left - 52.0f, bottom - 14.0f, 40.0f, 12.0f), juce::Justification::centredRight);
    }

    // the scale bar: a 1-2-5 length near 80 px, picked again on every resize
    const float um = niceMicrons (80.0f * umPerPx), len = um / umPerPx;
    const float y = bottom + 58.0f;
    // the light's calibration: what each step of grey means, for the method on the plate
    using M = rig::Recording::Method;
    const int method = processor.recording.method.load();
    struct Swatch { float level; juce::String label; };
    std::vector<Swatch> levels;
    switch (method)
    {
        case M::twoPhoton: levels = { { tone::floor, "at its own baseline" }, { deltaLevel (0.25f), "+25%" }, { deltaLevel (0.75f), "+75%" }, { tone::live, utf8 ("\xe2\x89\xa5 +100% \xce\x94""F/F") } }; break;
        case M::voltage: levels = { { tone::mid, "at rest" }, { tone::floor, "spiked this frame" }, { 0.04f, utf8 ("\xe2\x89\xa5 2 spikes") } }; break;
        case M::photonCounting: levels = { { 150.0f / 255.0f, "1 photon = 1 spike, last 1 s" } }; break;
        case M::tracts: levels = { { tone::floor * 1.4f, "the strongest wiring" }, { tone::live, "synapses x spikes, last 1 s" } }; break;
        case M::deoxyglucose: levels = { { tone::dim, "took up" }, { tone::mid, utf8 ("2\xc3\x97 other music") }, { tone::live, utf8 ("\xe2\x89\xa5 64\xc3\x97") } }; break;
        default: levels = { { tone::floor, "silent" }, { tone::dim, "fired this session" }, { tone::rate (10.0f), "10 Hz" }, { tone::live, utf8 ("\xe2\x89\xa5 100 Hz") } }; break;
    }
    const bool showScars = method == M::traceMethod || method == M::tracts;
    const auto scars = juce::String (processor.recording.scarTotal()) + (processor.recording.scarTotal() == 1 ? " scar" : " scars");
    // the bar and the legend start under the map's left edge; a narrower map (a taller atlas, the male's)
    // moves them left so the row still fits
    float x0 = left;
    {
        const auto& small = fonts->at (type::small);
        float width = len + 90.0f + (showScars ? 11.0f + juce::GlyphArrangement::getStringWidth (small, scars) : 0.0f);
        for (const auto& sw : levels)
            width += 10.0f + juce::GlyphArrangement::getStringWidth (small, sw.label) + 14.0f;
        x0 = std::max (14.0f + (float) referenceWidth, std::min (left, rail.getX() - 24.0f - width)); // never under the reference
    }
    g.setColour (tone::grey (tone::bright));
    line.h (x0, x0 + len, y);
    line.v (x0, y - 3.0f, y + 3.0f);
    line.v (x0 + len, y - 3.0f, y + 3.0f);
    g.setFont (fonts->at (type::data));
    g.drawText (microns (um), juce::Rectangle<float> (x0 + len + 8.0f, y - 8.0f, 80.0f, 16.0f), juce::Justification::centredLeft);

    {
        g.setFont (fonts->at (type::small));
        float x = x0 + len + 90.0f, ly = bottom + 58.0f;
        const float edge = (float) getWidth() - 12.0f; // a narrow plate (the reference open) wraps the legend
        for (const auto& sw : levels)
        {
            if (x + 10.0f + juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), sw.label) > edge && x > x0 + len + 90.0f)
                x = x0 + len + 90.0f, ly += 16.0f;
            g.setColour (tone::grey (tone::floor));
            g.drawRect (juce::Rectangle<float> (x - 1.0f, ly - 4.0f, 8.0f, 8.0f), line.width()); // a frame, so a dark step still reads
            g.setColour (tone::grey (sw.level));
            g.fillRect (juce::Rectangle<float> (x, ly - 3.0f, 6.0f, 6.0f));
            g.setColour (tone::grey (tone::mid));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), sw.label);
            g.drawText (sw.label, juce::Rectangle<float> (x + 10.0f, ly - 8.0f, w + 2.0f, 16.0f), juce::Justification::centredLeft);
            x += 10.0f + w + 14.0f;
        }
        if (showScars && x + 101.0f > edge)
            x = x0 + len + 90.0f, ly += 16.0f;
        if (showScars)
        {
            // a scar's mark, as the map draws it, settled
            g.setColour (tone::grey (tone::mid));
            g.fillRect (juce::Rectangle<float> (x, ly - 0.5f, 7.0f, 1.0f));
            g.fillRect (juce::Rectangle<float> (x + 3.0f, ly - 3.5f, 1.0f, 7.0f));
            g.drawText (scars, juce::Rectangle<float> (x + 11.0f, ly - 8.0f, 90.0f, 16.0f), juce::Justification::centredLeft);
        }
    }

    // what the picture is, and what it holds
    g.setColour (tone::grey (tone::dim));
    g.setFont (fonts->at (type::small));
    const auto dot = utf8 ("  \xc2\xb7  ");
    const auto counts = grouped (flydata::neurons) + " neurons" + dot + grouped (flydata::synapses) + " connections" + dot + grouped (synapseCount()) + " synapses";
    juce::String how;
    switch (method)
    {
        case M::twoPhoton: how = "two-photon" + dot + utf8 ("gcamp6s \xce\x94""F/F: each neuron against its own last 20 s"); break;
        case M::voltage: how = "voltage" + dot + utf8 ("asap2f: 2.5 ms, so a frame holds only its own spikes"); break;
        case M::photonCounting: how = "photon counting" + dot + utf8 ("gfp-aequorin: grains within 3 \xc2\xb5m"); break;
        case M::tracts: how = "tractography" + dot + "the " + grouped ((juce::int64) busyCount) + " busiest connections, last second" + dot + "curves drawn, links real"; break;
        case M::deoxyglucose:
        {
            double at = 0.0, lived = 0.0;
            const float* frame = lapsePicture (at, lived);
            how = utf8 ("\xc2\xb3h-2-deoxyglucose") + dot + "this session only" + dot + clock (at).fromFirstOccurrenceOf ("+", false, false);
            if (lapseStarted >= 0.0 && clockNow - lapseStarted < lapsePlaySeconds)
                how << " of " << clock (processor.recording.sessionSeconds()).fromFirstOccurrenceOf ("+", false, false);
            if (const auto most = topTypes (frame, lived, 3); most.isNotEmpty())
                how << dot << "most: " << most;
            break;
        }
        default: how = "trace" + dot + "300 ms"; break;
    }
    const bool first = std::abs (zoomNow - 1.0f) < 0.01f;
    const auto view = frontal() ? juce::String (first ? "frontal projection" : "frontal")
                                : "yaw " + juce::String (juce::roundToInt (yawNow)) + utf8 ("\xc2\xb0 pitch ") + juce::String (juce::roundToInt (pitchNow)) + utf8 ("\xc2\xb0");
    const auto field = first ? microns (widthMicrons) + utf8 (" \xc3\x97 ") + microns (heightMicrons)
                             : magnification (zoomNow) + dot + microns (widthMicrons / zoomNow) + utf8 (" \xc3\x97 ") + microns (heightMicrons / zoomNow);
    const auto what = (first ? how : how.upToFirstOccurrenceOf (dot, false, false)) + dot + view + dot + field;
    // top left, as on a slide's label
    const float cx = 14.0f + (float) referenceWidth;
    g.drawText (counts, juce::Rectangle<float> (cx, 8.0f, rail.getX() - 14.0f - cx, 16.0f), juce::Justification::centredLeft, true);
    if (hovered < 0)
        g.drawText (what, juce::Rectangle<float> (cx, 24.0f, rail.getX() - 14.0f - cx, 16.0f), juce::Justification::centredLeft, true);
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
        const char* term;
    };
    // the names stay inside the window: the row is spread so no two touch, then slid to fit
    // between the margins; if the full names can't fit side by side, the short ones are used
    const float leftLimit = 14.0f + (float) referenceWidth, rightLimit = rail.getX() - 12.0f; // never into the rail (3.2), nor under the reference (3.6)
    for (const bool above : { true, false })
    {
        std::vector<Placed> row;
        for (const bool shortNames : { false, true })
        {
            row.clear();
            for (const auto& r : regions)
            {
                if (r.above != above)
                    continue;
                for (int k = 0; k < flydata::anchorCount; ++k)
                    if (juce::String (flydata::anchors[k].name) == r.anchor)
                    {
                        const juce::Point<float> at { brain.getX() + brain.getWidth() * flydata::anchors[k].x,
                                                      brain.getY() + brain.getHeight() * flydata::anchors[k].y };
                        const juce::String text (shortNames ? r.shortName : r.name);
                        const float w = juce::GlyphArrangement::getStringWidth (font, text);
                        row.push_back ({ at, at.x - 0.5f * w, w, text, r.term });
                    }
            }
            std::sort (row.begin(), row.end(), [] (const Placed& a, const Placed& b) { return a.at.x < b.at.x; });

            // side by side in one row, never touching; below, where the regions crowd the midline,
            // the row is centred on them again so the leaders fan out both ways
            for (size_t k = 1; k < row.size(); ++k)
                row[k].x = std::max (row[k].x, row[k - 1].x + row[k - 1].w + 32.0f);
            if (row.empty())
                break;
            if (! above)
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
            const float rowLeft = row.front().x, rowRight = row.back().x + row.back().w;
            float shift = std::max (0.0f, leftLimit - rowLeft);
            shift -= std::max (0.0f, rowRight + shift - rightLimit);
            for (auto& p : row)
                p.x += shift;
            if (rowRight - rowLeft <= rightLimit - leftLimit)
                break;
        }

        if (! row.empty()) // never into the rail
        {
            const float over = row.back().x + row.back().w - (rail.getX() - 16.0f);
            if (over > 0.0f)
                for (auto& p : row)
                    p.x -= over;
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
            // a link (3.6): brighter under the hand, underlined when it is the focus
            const auto area = juce::Rectangle<float> (p.x, textY - 7.0f, p.w + 2.0f, 14.0f);
            const bool over = area.expanded (2.0f).contains (hoverAt);
            const bool picked = focus != nullptr && focus->active() && focus->key == juce::String (p.term);
            g.setColour (tone::grey (over || picked ? tone::live : tone::mid));
            g.drawText (p.text, area, juce::Justification::centredLeft);
            if (over || picked)
                line.h (p.x, p.x + p.w, textY + 7.0f);
            labels.push_back ({ area.expanded (2.0f), p.term });
        }
    }
}

void Specimen::lightPath (int cause, int move)
{
    pathCause = cause;
    pathMove = move;
    pathFrom = clockSeconds;
}

void Specimen::paintPath (juce::Graphics& g, const Hairline& line)
{
    // a new reflex lights its own path
    const auto& tl = processor.timeline;
    if (tl.reflexesEver() != reflexesLit)
    {
        reflexesLit = tl.reflexesEver();
        if (tl.reflexes() > 0)
            lightPath (tl.reflex (0).cause, tl.reflex (0).move);
    }
    const double age = clockSeconds - pathFrom;
    if (pathCause <= 0 || age > 2.0 || age < 0.0)
        return;
    const float fade = (float) (age < 1.4 ? 1.0 : 1.0 - (age - 1.4) / 0.6);

    auto anchor = [&] (const char* name) -> juce::Point<float> {
        for (int k = 0; k < flydata::anchorCount; ++k)
            if (juce::String (flydata::anchors[k].name) == name)
                return { brain.getX() + brain.getWidth() * flydata::anchors[k].x, brain.getY() + brain.getHeight() * flydata::anchors[k].y };
        return brain.getCentre();
    };
    // the rest of the brain steps back, so the path reads
    g.setColour (juce::Colours::black.withAlpha (0.55f * fade));
    g.fillRect (brain.expanded (8.0f));

    // the sound reaches its ears; the looming eye on that side (if one loomed); the giant fiber;
    // then down the neck to the tape, where the trace below shows the move
    std::vector<std::pair<juce::Point<float>, juce::String>> stops;
    // a kick on the sidechain looms in both low eyes at once, so its path starts there, not at the ears
    if (pathCause == 4)
    {
        stops.push_back ({ anchor ("eyeLeft"), "kick: LPLC2 left" });
        stops.push_back ({ anchor ("eyeRight"), "LPLC2 right" });
    }
    else
        stops.push_back ({ anchor ("ears"), "JO" });
    if (pathCause == 1)
        stops.push_back ({ anchor ("eyeLeft"), "LPLC2 left" });
    else if (pathCause == 2)
        stops.push_back ({ anchor ("eyeRight"), "LPLC2 right" });
    stops.push_back ({ anchor ("giantFiber"), "DNp01" });
    const auto neck = juce::Point<float> (stops.back().first.x, brain.getBottom());

    juce::Path p;
    if (pathCause == 4)
    {
        // both eyes feed the giant fiber side by side
        p.startNewSubPath (stops[0].first);
        p.lineTo (stops[2].first);
        p.startNewSubPath (stops[1].first);
        p.lineTo (stops[2].first);
    }
    else
    {
        p.startNewSubPath (stops.front().first);
        for (size_t k = 1; k < stops.size(); ++k)
            p.lineTo (stops[k].first);
    }
    p.lineTo (neck);
    if (bare > 0.0f)
    {
        // 3.7, lights out: only the lines, drawn as light through tissue (a soft halo under a fine core)
        for (const auto& [width, alpha] : { std::pair<float, float> { 9.0f, 0.05f }, { 5.0f, 0.10f }, { 2.5f, 0.22f } })
        {
            g.setColour (juce::Colours::white.withAlpha (alpha * fade * bare));
            g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    g.setColour (tone::grey (tone::live).withAlpha (fade));
    g.strokePath (p, juce::PathStrokeType (1.5f * line.width(), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (bare >= 0.999f)
    {
        for (const auto& s : stops) // where it passed: a point of light, no name
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f * fade));
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (s.first));
            g.setColour (tone::grey (tone::live).withAlpha (fade));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (s.first));
        }
        return;
    }
    const float named = fade * (1.0f - bare);
    const auto small = fonts->at (type::small);
    g.setFont (small);
    for (size_t k = 0; k < stops.size(); ++k)
    {
        const auto at = stops[k].first;
        g.setColour (tone::black);
        g.fillRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (at));
        g.setColour (tone::grey (tone::live).withAlpha (named));
        g.drawRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (at), line.width());
        const float w = juce::GlyphArrangement::getStringWidth (small, stops[k].second) + 4.0f;
        const bool leftSide = at.x < brain.getCentreX() && k + 1 < stops.size();
        g.drawText (stops[k].second, juce::Rectangle<float> (leftSide ? at.x - 8.0f - w : at.x + 8.0f, at.y - 6.0f, w, 12.0f),
                    leftSide ? juce::Justification::centredRight : juce::Justification::centredLeft);
    }
    // what it did, under the giant fiber's name: the same move the trace below marks
    const auto gf = stops.back().first;
    const juce::String what = utf8 ("\xe2\x86\x93 ") + rig::Timeline::moveName (pathMove) + utf8 (" \xc2\xb7 the tape");
    g.setColour (tone::grey (tone::bright).withAlpha (named));
    g.drawText (what, juce::Rectangle<float> (gf.x + 8.0f, gf.y + 8.0f, 200.0f, 12.0f), juce::Justification::centredLeft);
}

//==============================================================================
// the tracts and the rail

int Specimen::methodAt (juce::Point<float> p) const noexcept
{
    if (! methodList.contains (p))
        return -1;
    return juce::jlimit (0, rig::Recording::methods - 1, (int) ((p.y - methodList.getY()) / railRow));
}

void Specimen::paintRail (juce::Graphics& g, const Hairline& line)
{
    auto& rec = processor.recording;
    const int method = rec.method.load();
    const auto dot = utf8 ("  \xc2\xb7  ");
    auto title = [&] (float y, const juce::String& text) {
        g.setFont (fonts->at (type::small));
        g.setColour (tone::grey (tone::mid));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        g.drawText (text, juce::Rectangle<float> (rail.getX(), y, w + 2.0f, 14.0f), juce::Justification::centredLeft);
        g.setColour (tone::grey (tone::floor));
        line.h (rail.getX() + w + 8.0f, rail.getRight(), y + 7.0f);
    };

    // the method: one line each, the one on the plate bright with a bar beside it
    title (rail.getY(), "imaging");
    g.setFont (fonts->at (type::data));
    for (int m = 0; m < rig::Recording::methods; ++m)
    {
        const auto row = juce::Rectangle<float> (methodList.getX(), methodList.getY() + railRow * (float) m, methodList.getWidth(), railRow);
        const bool on = m == method, over = row.contains (hoverAt);
        g.setColour (tone::grey (on ? tone::live : over ? tone::bright : tone::dim));
        g.drawText (rig::Recording::methodName (m), row.withTrimmedLeft (12.0f), juce::Justification::centredLeft);
        if (on)
            g.fillRect (juce::Rectangle<float> (row.getX(), row.getCentreY() - 4.0f, 2.0f, 8.0f));
    }

    // 2-DG: this session's uptake, always seen from the front; pressing it plays the time-lapse
    title (insetArea.getY() - 22.0f, "2-dg" + dot + "this session");
    {
        const int w = std::max (1, juce::roundToInt (insetArea.getWidth() * line.px)), h = std::max (1, juce::roundToInt (insetArea.getHeight() * line.px));
        if (inset.getWidth() != w || inset.getHeight() != h)
            inset = juce::Image (juce::Image::RGB, w, h, true);
        std::vector<float> now ((size_t) neurons);
        for (int n = 0; n < neurons; ++n)
            now[(size_t) n] = rec.uptake (n);
        renderUptake (inset, now.data(), rec.steppedSeconds(), true, widthMicrons / (float) w, {}, std::max (1, juce::roundToInt (line.px)));
        g.drawImageTransformed (inset, juce::AffineTransform::scale (1.0f / line.px).translated (line.snap (insetArea.getX()) - 0.5f / line.px, line.snap (insetArea.getY()) - 0.5f / line.px));
        g.setColour (tone::grey (method == rig::Recording::deoxyglucose || insetArea.contains (hoverAt) ? tone::bright : tone::floor));
        g.drawRect (insetArea.expanded (2.0f), line.width());
        g.setFont (fonts->at (type::small));
        g.setColour (tone::grey (tone::dim));
        const auto spikes = rec.spikesInSession();
        const auto under = juce::Rectangle<float> (rail.getX(), insetArea.getBottom() + 5.0f, rail.getWidth(), 14.0f);
        auto elapsed = clock (rec.sessionSeconds()).fromFirstOccurrenceOf ("+", false, false).upToLastOccurrenceOf (".", false, false);
        if (elapsed.startsWith ("00:"))
            elapsed = elapsed.substring (3); // under an hour: minutes and seconds
        g.drawText (elapsed, under, juce::Justification::centredLeft);
        g.drawText ((spikes >= 1000000 ? juce::String ((double) spikes / 1.0e6, 1) + " M" : spikes >= 100000 ? juce::String (juce::roundToInt ((double) spikes / 1.0e3)) + " k" : grouped (spikes)) + " spikes",
                    under, juce::Justification::centredRight);
        // 3.6: where this session went that other music would not, by cell type
        const auto most = topTypes (now.data(), rec.steppedSeconds(), 3);
        g.setColour (tone::grey (most.isEmpty() ? tone::floor : tone::mid));
        g.drawFittedText (most.isEmpty() ? juce::String ("nothing beyond other music yet") : "most: " + most, under.translated (0.0f, 14.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.75f);
    }

    // the view: how the brain is turned and magnified, and how to change it (no dial: the brain is the handle)
    // in a short window the legend row under the map runs under the rail: stop above it
    const float floor = std::min (rail.getBottom(), brain.getBottom() + 44.0f);
    if (insetArea.getBottom() + 48.0f + 22.0f + railRow <= floor)
    {
        const float y = insetArea.getBottom() + 48.0f;
        title (y, "view");
        auto angle = [] (float v) { const int d = juce::roundToInt (v); return (d > 0 ? "+" : "") + juce::String (d) + utf8 ("\xc2\xb0"); };
        g.setFont (fonts->at (type::data));
        auto row = [&] (int k, const juce::String& name, const juce::String& value) {
            const auto r = juce::Rectangle<float> (rail.getX(), y + 22.0f + railRow * (float) k, rail.getWidth(), railRow);
            g.setColour (tone::grey (tone::dim));
            g.drawText (name, r, juce::Justification::centredLeft);
            g.setColour (tone::grey (tone::bright));
            g.drawText (value, r, juce::Justification::centredRight);
        };
        if (y + 22.0f + railRow * 3.0f > floor) // no room for three rows: one
        {
            row (0, angle (yawNow) + " " + angle (pitchNow), magnification (zoomNow));
            return;
        }
        row (0, "yaw", angle (yawNow));
        row (1, "pitch", angle (pitchNow));
        row (2, "magnified", magnification (zoomNow));
        g.setFont (fonts->at (type::small));
        g.setColour (tone::grey (tone::dim));
        auto hint = juce::Rectangle<float> (rail.getX(), y + 22.0f + railRow * 3.0f + 8.0f, rail.getWidth(), 14.0f);
        for (const char* h : { "drag turns, wheel zooms", "double-click: home" })
        {
            if (hint.getBottom() > floor)
                break;
            g.drawFittedText (h, hint.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            hint.translate (0.0f, 14.0f);
        }
    }
}

//==============================================================================
// the light

juce::Point<float> Specimen::spotPoint() const noexcept
{
    return toScreen (processor.recording.lightX.load() * widthMicrons, processor.recording.lightY.load() * heightMicrons);
}

Specimen::Part Specimen::partAt (juce::Point<float> p) const noexcept
{
    if (rail.contains (p))
        return Part::none;
    int which = 0;
    if (dialAt (p, which) >= 0)
        return Part::dial;
    if (noElectrode)
        return Part::none;
    const float d = p.getDistanceFrom (spotPoint()), r = spotRadius();
    if (d < 7.0f)
        return Part::centre;
    if (std::abs (d - r) < 6.0f)
        return Part::ring;
    if (d < r)
        return Part::inside;
    return Part::none;
}

void Specimen::measureLight()
{
    // the neurons under the spot: the light passes through the whole depth, so what the projection
    // puts under it is lit (as one-photon light lights a whole column)
    const auto& rec = processor.recording;
    const float lx = rec.lightX.load() * widthMicrons, ly = rec.lightY.load() * heightMicrons;
    const float r = spotMicrons (space), r2 = r * r;
    Recorded out;
    out.xMicrons = lx;
    out.yMicrons = heightMicrons - ly;
    out.radiusMicrons = r;
    lit.clear();
    float sum = 0.0f;
    for (int n = 0; n < neurons; ++n)
    {
        const float dx = projUm[(size_t) (2 * n)] - lx, dy = projUm[(size_t) (2 * n + 1)] - ly;
        if (dx * dx + dy * dy > r2)
            continue;
        lit.push_back ((uint16_t) n);
        const float hz = rec.trace (n) / (float) rig::Recording::traceSeconds;
        sum += hz;
        if (out.busiest < 0 || hz > out.busiestHz)
        {
            out.busiest = n;
            out.busiestHz = hz;
        }
    }
    out.units = (int) lit.size();
    out.meanHz = out.units > 0 ? sum / (float) out.units : 0.0f;
    tip = out;
    // the fly is told which neurons the light covers whenever that changes
    if (lit != litSent)
        litPending = true;
    if (litPending && processor.postLit (lit.data(), (int) lit.size()))
    {
        litSent = lit;
        litPending = false;
    }
}

void Specimen::paintLight (juce::Graphics& g, const Hairline& line)
{
    const auto at = spotPoint();
    const float r = spotRadius();
    const bool flash = throwOn || touching;
    const bool hot = hoverPart != Part::none && hoverPart != Part::dial;

    if (! brain.expanded (r).contains (at))
    {
        // out of the field (magnified elsewhere): a mark on the field's edge, toward it
        const auto edge = juce::Point<float> (juce::jlimit (brain.getX(), brain.getRight(), at.x), juce::jlimit (brain.getY(), brain.getBottom(), at.y));
        const auto toward = (at - edge) / std::max (1.0f, at.getDistanceFrom (edge));
        const auto side = juce::Point<float> (-toward.y, toward.x);
        juce::Path mark;
        mark.addTriangle (edge + toward * 2.0f, edge - toward * 7.0f + side * 4.0f, edge - toward * 7.0f - side * 4.0f);
        g.setColour (tone::grey (flash ? tone::live : tone::bright));
        g.strokePath (mark, juce::PathStrokeType (line.width()));
        return;
    }

    {
        // the light itself (3.6): no disc, a beam. Its gaussian profile in the air of the plate,
        // brighter with power; a flash blooms past its edge. The tissue under it lights in the
        // picture (lightTissue).
        juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (brain.toNearestInt());
        const float peak = flash ? 0.34f : 0.05f + 0.11f * feed;
        auto beam = [&] (float radius, float top) {
            juce::ColourGradient glow (juce::Colours::white.withAlpha (top), at.x, at.y, juce::Colours::white.withAlpha (0.0f), at.x + radius, at.y, true);
            for (float q : { 0.2f, 0.4f, 0.6f, 0.8f }) // exp(-2 q^2): the beam's own falloff
                glow.addColour (q, juce::Colours::white.withAlpha (top * std::exp (-4.5f * q * q)));
            g.setGradientFill (glow);
            g.fillEllipse (juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (at));
        };
        beam (1.5f * r, peak);
        if (flash)
            beam (2.6f * r, 0.12f); // the bloom
    }

    // the stimulus: a hairline from where the hand presses to the spot, and the press site
    if (touching)
    {
        g.setColour (tone::grey (tone::bright));
        const auto toward = at + (pressAt - at) * (r / std::max (r, pressAt.getDistanceFrom (at)));
        g.drawLine (juce::Line<float> (pressAt, toward), line.width());
        g.drawRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ line.snap (pressAt.x), line.snap (pressAt.y) }), line.width());
    }

    // its handles only while the hand is near it or holds it: the edge (size: space), the power
    // around it (feed), from 7:30 round to 4:30, the centre; then the numbers beside it
    const bool near = dragPart == Part::ring || dragPart == Part::inside || dragPart == Part::centre
                   || (hoverAt.x >= 0.0f && hoverAt.getDistanceFrom (at) < r + 28.0f && ! rail.contains (hoverAt));
    const auto centre = juce::Point<float> (line.snap (at.x), line.snap (at.y));
    if (! near)
    {
        // resting: only a faint edge and its centre, so the hand can find it
        g.setColour (tone::grey (tone::floor));
        g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre), line.width());
        g.setColour (tone::grey (tone::dim));
        g.fillRect (juce::Rectangle<float> (2.0f, 2.0f).withCentre (centre));
    }
    else
    {
    g.setColour (tone::grey (flash || hot || dragPart == Part::ring ? tone::live : tone::bright));
    g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre), line.width());
    const float a0 = juce::MathConstants<float>::pi * 0.75f, sweep = juce::MathConstants<float>::pi * 1.5f;
    g.setColour (tone::grey (tone::dim));
    for (int k = 0; k <= 10; ++k)
    {
        const float a = a0 + sweep * (float) k / 10.0f, len = k % 5 == 0 ? 6.0f : 3.0f;
        g.drawLine (centre.x + (r + 5.0f) * std::cos (a), centre.y + (r + 5.0f) * std::sin (a),
                    centre.x + (r + 5.0f + len) * std::cos (a), centre.y + (r + 5.0f + len) * std::sin (a), line.width());
    }
    juce::Path power;
    power.addCentredArc (centre.x, centre.y, r + 5.0f, r + 5.0f, 0.0f, a0 + juce::MathConstants<float>::halfPi, a0 + juce::MathConstants<float>::halfPi + sweep * feed, true);
    g.setColour (tone::grey (dragPart == Part::inside || flash ? tone::live : tone::bright));
    g.strokePath (power, juce::PathStrokeType (2.0f * line.width()));
    g.setColour (tone::grey (dragPart == Part::centre || hoverPart == Part::centre ? tone::live : tone::bright));
    g.drawRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre (centre), line.width());

    // what it is, beside it: size, lit, power, the flash's rate (not with the lights out: no numbers)
    if (bare < 0.5f)
    {
    g.setFont (fonts->at (type::small));
    const auto dot = utf8 ("  \xc2\xb7  ");
    const juce::String l1 = "light " + microns (2.0f * spotMicrons (space)) + dot + grouped (tip.units) + " lit";
    const juce::String l2 = "power " + juce::String (juce::roundToInt (feed * 100.0f)) + "%" + dot + juce::String (juce::roundToInt (Engine::flashHz (feed))) + " Hz flash";
    const float w = std::max (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), l1), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), l2)) + 4.0f;
    float x = centre.x + r + 18.0f;
    if (x + w > rail.getX() - 12.0f)
        x = std::max (brain.getX() + 4.0f, centre.x - r - 18.0f - w);
    const float y = juce::jlimit (brain.getY(), brain.getBottom() - 28.0f, centre.y - 14.0f);
    g.setColour (juce::Colours::black.withAlpha (0.92f)); // read over the neurons
    g.fillRect (juce::Rectangle<float> (x - 3.0f, y - 1.0f, w + 6.0f, 30.0f));
    g.setColour (tone::grey (flash ? tone::live : tone::mid));
    g.drawText (l1, juce::Rectangle<float> (x, y, w, 14.0f), juce::Justification::centredLeft);
    g.setColour (tone::grey (tone::dim));
    g.drawText (l2, juce::Rectangle<float> (x, y + 14.0f, w, 14.0f), juce::Justification::centredLeft);
    }
    }

    // where it sits, marked on the calibration axes (they leave with the bench)
    if (bare > 0.0f)
        return;
    const float left = brain.getX() - 10.0f, bottom = brain.getBottom() + 10.0f;
    g.setColour (tone::grey (tone::bright));
    line.h (left - 9.0f, left, at.y);
    line.v (at.x, bottom, bottom + 9.0f);
}

//==============================================================================
// the atlas: magnified, each region opens its own numbers

void Specimen::paintSites (juce::Graphics& g, const Hairline& line)
{
    const bool showing = zoomNow >= sitesOpenAt;
    std::vector<juce::Rectangle<float>> cards;
    for (auto& site : sites)
    {
        site.open = false;
        if (! showing || site.neurons.empty())
            continue;
        juce::Point<float> c;
        for (auto n : site.neurons)
            c += proj[(size_t) n];
        c /= (float) site.neurons.size();
        site.at = c;
        if (! brain.reduced (12.0f).contains (c))
            continue;
        const int dials = site.params[1] >= 0 ? 2 : 1;
        const float dial = 40.0f, cell = 74.0f;
        const float w = std::max (cell * (float) dials, juce::GlyphArrangement::getStringWidth (fonts->at (type::small), site.name) + 16.0f), h = 22.0f + dial + 34.0f;
        auto card = juce::Rectangle<float> (c.x + 18.0f, c.y - 0.5f * h, w, h);
        if (card.getRight() > brain.getRight())
            card.setX (c.x - 18.0f - w);
        card.setY (juce::jlimit (brain.getY() + 2.0f, brain.getBottom() - h - 2.0f, card.getY()));
        for (const auto& other : cards) // never on top of another region's card
            if (card.intersects (other))
                card.setY (other.getBottom() + 6.0f);
        if (card.getBottom() > brain.getBottom() || card.getX() < brain.getX())
            continue;
        cards.push_back (card);
        site.open = true;

        // its neurons ringed, a leader to the card
        g.setColour (tone::grey (tone::mid));
        for (auto n : site.neurons)
            if (brain.contains (proj[(size_t) n]) && site.neurons.size() <= 40)
                g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (proj[(size_t) n]), line.width());
        const auto toCard = juce::Point<float> (card.getX() < c.x ? card.getRight() : card.getX(), card.getY() + 11.0f);
        g.drawLine (juce::Line<float> (c, toCard), line.width());
        g.fillRect (juce::Rectangle<float> (3.0f, 3.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRect (card);
        g.setColour (tone::grey (tone::floor));
        g.drawRect (card, line.width());
        g.setFont (fonts->at (type::small));
        g.setColour (tone::grey (tone::bright));
        g.drawText (site.name, card.withHeight (20.0f).reduced (8.0f, 0.0f), juce::Justification::centredLeft);

        for (int k = 0; k < dials; ++k)
        {
            auto* param = taste[(size_t) site.params[k]];
            const auto& def = DendriteProcessor::tasteParams[(size_t) site.params[k]];
            const auto box = juce::Rectangle<float> (card.getX() + cell * (float) k, card.getY() + 22.0f, cell, dial + 30.0f);
            const auto d = juce::Rectangle<float> (dial, dial).withCentre ({ box.getCentreX(), box.getY() + 0.5f * dial });
            site.dials[k] = d.expanded (6.0f);
            const bool on = (dragPart == Part::dial && &sites[(size_t) dragSite] == &site && dragDial == k) || site.dials[k].contains (hoverAt);
            const float a0 = juce::MathConstants<float>::pi * 1.25f, sweep = juce::MathConstants<float>::pi * 1.5f;
            juce::Path track, value;
            track.addCentredArc (d.getCentreX(), d.getCentreY(), 0.5f * dial, 0.5f * dial, 0.0f, a0, a0 + sweep, true);
            value.addCentredArc (d.getCentreX(), d.getCentreY(), 0.5f * dial, 0.5f * dial, 0.0f, a0, a0 + sweep * param->getValue(), true);
            g.setColour (tone::grey (tone::floor));
            g.strokePath (track, juce::PathStrokeType (line.width()));
            g.setColour (tone::grey (on ? tone::live : tone::bright));
            g.strokePath (value, juce::PathStrokeType (2.0f * line.width()));
            // the fly's own value (the middle): a tick
            const float am = a0 + sweep * 0.5f - juce::MathConstants<float>::halfPi;
            g.setColour (tone::grey (tone::dim));
            g.drawLine (d.getCentreX() + 0.5f * dial * std::cos (am) * 0.78f, d.getCentreY() + 0.5f * dial * std::sin (am) * 0.78f,
                        d.getCentreX() + 0.5f * dial * std::cos (am), d.getCentreY() + 0.5f * dial * std::sin (am), line.width());
            juce::String label (def.name);
            label = label.fromLastOccurrenceOf (" ", false, false);
            g.setColour (tone::grey (tone::mid));
            g.drawText (label, d.withSizeKeepingCentre (dial, 14.0f), juce::Justification::centred);
            g.setColour (tone::grey (on ? tone::live : tone::bright));
            g.drawText (param->getCurrentValueAsText(), juce::Rectangle<float> (box.getX(), d.getBottom() + 4.0f, cell, 14.0f), juce::Justification::centred);
        }
    }
}

int Specimen::dialAt (juce::Point<float> p, int& which) const noexcept
{
    if (bare > 0.5f)
        return -1; // lights out: the atlas's cards are not there
    for (size_t s = 0; s < sites.size(); ++s)
        if (sites[s].open)
            for (int k = 0; k < (sites[s].params[1] >= 0 ? 2 : 1); ++k)
                if (sites[s].dials[k].contains (p))
                {
                    which = k;
                    return (int) s;
                }
    return -1;
}

//==============================================================================
// the hand

void Specimen::tick (double nowSeconds)
{
    clockSeconds = nowSeconds;
    const double dt = lastTick < 0.0 ? 0.0 : juce::jlimit (0.0, 0.1, nowSeconds - lastTick);
    lastTick = nowSeconds;
    clockNow = nowSeconds;
    // a still press becomes a flash once it has lasted a moment, so a click that only focuses the window is not one
    if (pending && nowSeconds - pressStarted >= 0.08)
    {
        pending = false;
        touching = true;
        touchStarted = nowSeconds;
        setThrow (true);
    }
    // a flick keeps the brain turning, slowing as a stage on its bearing does
    if (! turning && (spinYaw != 0.0f || spinPitch != 0.0f))
    {
        turnBy (spinYaw * (float) dt, spinPitch * (float) dt);
        const float keep = std::exp (-1.4f * (float) dt);
        spinYaw *= keep;
        spinPitch *= keep;
        if (std::abs (spinYaw) + std::abs (spinPitch) < 3.0f)
            spinYaw = spinPitch = 0.0f;
    }
    // going home: eased over about a fifth of a second
    if (easing)
    {
        const float k = 1.0f - std::exp (-14.0f * (float) dt);
        zoomNow = std::exp (std::log (zoomNow) + (std::log (zoomGoal) - std::log (zoomNow)) * k);
        zoomCx += (goalCx - zoomCx) * k;
        zoomCy += (goalCy - zoomCy) * k;
        if (std::abs (std::log (zoomNow / zoomGoal)) < 0.002f && std::abs (goalCx - zoomCx) + std::abs (goalCy - zoomCy) < 0.05f)
        {
            zoomNow = zoomGoal;
            zoomCx = goalCx;
            zoomCy = goalCy;
            easing = false;
        }
        clampField();
        storeField();
    }
}

void Specimen::setThrow (bool on)
{
    throwOn = on;
    throwLink.setValueAsCompleteGesture (on ? 1.0f : 0.0f);
}

juce::String Specimen::getTooltip()
{
    if (bare > 0.5f)
        return {}; // lights out: no words
    const auto dot = utf8 (" \xc2\xb7 ");
    if (methodAt (hoverAt) >= 0)
        return "imaging: what the plate shows of the same spikes";
    if (insetArea.expanded (2.0f).contains (hoverAt))
        return "2-dg: this session only, never saved; bright where it worked a neuron more than twice what other music does" + dot + "press: play how it built up";
    if (rail.contains (hoverAt))
        return {};
    switch (hoverPart)
    {
        case Part::dial:
        {
            int which = 0;
            const int s = dialAt (hoverAt, which);
            if (s < 0)
                return {};
            const auto& def = DendriteProcessor::tasteParams[(size_t) sites[(size_t) s].params[which]];
            return juce::String (def.name) + ": drag up or down" + dot + "the tick is the fly's own" + dot + "double-click: back to it";
        }
        case Part::centre: return "the light: drag to move it over other neurons";
        case Part::ring: return "the light's size: space (" + DendriteProcessor::spaceName (space) + " " + juce::String (juce::roundToInt (space * 100.0f)) + "%)" + dot + "drag the edge";
        case Part::inside: return "the light's power: feed (" + DendriteProcessor::feedName (feed) + " " + juce::String (juce::roundToInt (feed * 100.0f))
                                  + "%)" + dot + "drag up or down" + dot + "shift: fine" + dot + "double-click: home";
        case Part::none: break;
    }
    if (hoverPlate)
        return "press and hold: a flash (the throw; the lit neurons fire)" + dot + "drag: turn the brain" + dot + "scroll: magnify" + dot + "double-click: home";
    return {};
}

void Specimen::mouseMove (const juce::MouseEvent& e)
{
    hoverAt = e.position;
    if (rail.contains (e.position))
    {
        hoverPart = Part::none;
        hoverPlate = false;
        setMouseCursor (methodAt (e.position) >= 0 || insetArea.expanded (2.0f).contains (e.position) ? juce::MouseCursor::PointingHandCursor
                                                                                                    : juce::MouseCursor::NormalCursor);
        return;
    }
    hoverPart = partAt (e.position);
    hoverPlate = hoverPart == Part::none;
    bool link = tagNeuron >= 0 && tagArea.contains (e.position);
    for (const auto& l : labels)
        link = link || l.area.contains (e.position);
    if (link && hoverPart == Part::none)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        return;
    }
    setMouseCursor (hoverPart == Part::centre ? juce::MouseCursor::DraggingHandCursor
                    : hoverPart == Part::ring ? juce::MouseCursor::LeftRightResizeCursor
                    : hoverPart == Part::inside || hoverPart == Part::dial ? juce::MouseCursor::UpDownResizeCursor
                                                                           : juce::MouseCursor::CrosshairCursor);
}

void Specimen::mouseExit (const juce::MouseEvent&)
{
    hoverAt = { -1.0f, -1.0f };
    hoverPart = Part::none;
    hoverPlate = false;
}

void Specimen::mouseDown (const juce::MouseEvent& e)
{
    auto& rec = processor.recording;
    spinYaw = spinPitch = 0.0f; // a hand on the brain stops it
    if (const int m = methodAt (e.position); m >= 0)
    {
        rec.method = m;
        if (m == rig::Recording::deoxyglucose)
            lapseStarted = clockNow;
        return;
    }
    if (insetArea.expanded (2.0f).contains (e.position))
    {
        rec.method = (int) rig::Recording::deoxyglucose;
        lapseStarted = clockNow; // from the session's first second to now, again
        return;
    }
    if (rail.contains (e.position))
        return;
    // 3.6: a neuron's tag opens its type; a region's name, its entry
    if (bare < 0.5f && tagNeuron >= 0 && tagArea.contains (e.position))
    {
        if (onPickType)
            onPickType ((int) rig::identities::type[tagNeuron]);
        return;
    }
    for (const auto& l : labels)
        if (l.area.contains (e.position))
        {
            if (onPickTerm)
                onPickTerm (l.term);
            return;
        }
    if (e.mods.isPopupMenu() || e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        turning = true;
        lastTurnAt = e.position;
        lastTurnTime = clockNow;
        turnedYaw = turnedPitch = 0.0f;
        return;
    }
    if (e.mods.isMiddleButtonDown() || e.mods.isAltDown())
    {
        panning = true;
        easing = false;
        panFrom = e.position;
        panCentreFrom = { zoomCx, zoomCy };
        return;
    }
    fine = e.mods.isShiftDown();
    grabMouse = e.position;
    dragPart = partAt (e.position);
    switch (dragPart)
    {
        case Part::dial:
        {
            dragSite = dialAt (e.position, dragDial);
            auto* param = taste[(size_t) sites[(size_t) dragSite].params[dragDial]];
            grabValue = param->getValue();
            param->beginChangeGesture();
            break;
        }
        case Part::centre: grabUnit = { rec.lightX.load(), rec.lightY.load() }; break;
        case Part::ring: spaceLink.beginGesture(); break;
        case Part::inside: grabValue = feed; feedLink.beginGesture(); break;
        case Part::none:
            // still: a flash; moved: the brain turns
            pending = true;
            pressStarted = clockNow;
            pressAt = e.position;
            break;
    }
}

void Specimen::mouseDrag (const juce::MouseEvent& e)
{
    if (pending && e.position.getDistanceFrom (pressAt) > 4.0f)
    {
        // it moved before it became a flash: the hand has the brain, and turns it
        pending = false;
        turning = true;
        lastTurnAt = pressAt;
        lastTurnTime = clockNow;
        turnedYaw = turnedPitch = 0.0f;
    }
    if (turning)
    {
        // across three quarters of the plate is a half turn, as if the hand rolled a ball under it
        const float perPixel = 180.0f / std::max (1.0f, brain.getWidth() / 1.5f) * (e.mods.isShiftDown() ? 0.2f : 1.0f);
        const auto moved = e.position - lastTurnAt;
        const float dYaw = moved.x * perPixel, dPitch = -moved.y * perPixel;
        turnBy (dYaw, dPitch);
        lastTurnAt = e.position;
        turnedYaw += dYaw;
        turnedPitch += dPitch;
        const double dt = clockNow - lastTurnTime;
        if (dt > 0.004)
        {
            // the hand's speed, smoothed over the last few moves, for the flick
            const float k = (float) std::min (1.0, dt / 0.05);
            spinYaw += (turnedYaw / (float) dt - spinYaw) * k;
            spinPitch += (turnedPitch / (float) dt - spinPitch) * k;
            lastTurnTime = clockNow;
            turnedYaw = turnedPitch = 0.0f;
        }
        return;
    }
    if (panning)
    {
        const float s = pixelsPerMicron();
        zoomCx = panCentreFrom.x - (e.position.x - panFrom.x) / s;
        zoomCy = panCentreFrom.y - (e.position.y - panFrom.y) / s;
        clampField();
        storeField();
        return;
    }
    if (touching)
    {
        pressAt = e.position;
        return;
    }
    if (e.mods.isShiftDown() != fine)
    {
        // fine starts again from here, so nothing jumps
        fine = e.mods.isShiftDown();
        grabMouse = e.position;
        grabValue = dragPart == Part::inside ? feed : dragPart == Part::dial ? taste[(size_t) sites[(size_t) dragSite].params[dragDial]]->getValue() : grabValue;
        grabUnit = dragPart == Part::centre ? juce::Point<float> (processor.recording.lightX.load(), processor.recording.lightY.load()) : grabUnit;
    }
    const float scale = fine ? 0.2f : 1.0f;
    const auto moved = e.position - grabMouse;
    switch (dragPart)
    {
        case Part::dial:
            taste[(size_t) sites[(size_t) dragSite].params[dragDial]]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, grabValue - moved.y / 160.0f * scale));
            break;
        case Part::centre:
        {
            // in microns, so magnified the same hand moves the light less
            const float s = pixelsPerMicron();
            processor.recording.lightX = juce::jlimit (0.0f, 1.0f, grabUnit.x + moved.x * scale / s / widthMicrons);
            processor.recording.lightY = juce::jlimit (0.0f, 1.0f, grabUnit.y + moved.y * scale / s / heightMicrons);
            break;
        }
        case Part::ring:
        {
            const float um = e.position.getDistanceFrom (spotPoint()) / pixelsPerMicron();
            space = juce::jlimit (0.0f, 1.0f, std::log (std::max (um, 1.0f) / 8.0f) / std::log (150.0f / 8.0f));
            spaceLink.setValueAsPartOfGesture (space);
            break;
        }
        case Part::inside:
            feed = juce::jlimit (0.0f, 1.0f, grabValue - moved.y / 200.0f * scale);
            feedLink.setValueAsPartOfGesture (feed);
            break;
        case Part::none: break;
    }
}

void Specimen::mouseUp (const juce::MouseEvent&)
{
    if (turning)
    {
        // a flick: released while still moving, the brain keeps turning; a hand that stopped first lets it rest
        if (clockNow - lastTurnTime > 0.08 || std::abs (spinYaw) + std::abs (spinPitch) < 30.0f)
            spinYaw = spinPitch = 0.0f;
        spinYaw = juce::jlimit (-360.0f, 360.0f, spinYaw);
        spinPitch = juce::jlimit (-180.0f, 180.0f, spinPitch);
    }
    turning = panning = false;
    if (touching)
        setThrow (false);
    touching = pending = false;
    switch (dragPart)
    {
        case Part::dial: taste[(size_t) sites[(size_t) dragSite].params[dragDial]]->endChangeGesture(); break;
        case Part::ring: spaceLink.endGesture(); break;
        case Part::inside: feedLink.endGesture(); break;
        case Part::centre:
        case Part::none: break;
    }
    dragPart = Part::none;
}

void Specimen::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (rail.contains (e.position))
        return;
    int which = 0;
    if (const int s = dialAt (e.position, which); s >= 0)
    {
        auto* param = taste[(size_t) sites[(size_t) s].params[which]];
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getDefaultValue()); // the fly's own again
        param->endChangeGesture();
        return;
    }
    const auto part = partAt (e.position);
    if (part == Part::none)
    {
        viewHome(); // frontal and whole
        return;
    }
    feedLink.setValueAsCompleteGesture (DendriteProcessor::defaultFeed);
    spaceLink.setValueAsCompleteGesture (DendriteProcessor::defaultSpace);
}

void Specimen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (rail.contains (e.position))
        return;
    const float sign = w.isReversed ? -1.0f : 1.0f;
    const float delta = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
    if (delta == 0.0f)
        return;
    const auto part = partAt (e.position);
    if (part == Part::dial)
    {
        int which = 0;
        const int s = dialAt (e.position, which);
        auto* param = taste[(size_t) sites[(size_t) s].params[which]];
        param->beginChangeGesture();
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, param->getValue() + (e.mods.isShiftDown() ? 0.01f : 0.04f) * sign * (delta > 0.0f ? 1.0f : -1.0f)));
        param->endChangeGesture();
        return;
    }
    if (part != Part::none)
    {
        // on the light: its power (shift: its size)
        const float step = 0.04f * sign * (delta > 0.0f ? 1.0f : -1.0f);
        if (e.mods.isShiftDown())
            spaceLink.setValueAsCompleteGesture (juce::jlimit (0.0f, 1.0f, space + step));
        else
            feedLink.setValueAsCompleteGesture (juce::jlimit (0.0f, 1.0f, feed + step));
        return;
    }
    // anywhere else on the brain: magnify about the hand
    const float octaves = juce::jlimit (-1.0f, 1.0f, sign * delta * 2.0f);
    // 3.7: pushed on out past the smallest magnification (about an octave and a half more, within a
    // second between pushes), the bench steps away: lights out. A stop the hand must push through.
    if (octaves < 0.0f && zoomNow <= rig::Recording::minZoom + 1.0e-3f && bare < 0.5f && onStepBack)
    {
        if (clockSeconds - lastPushAt > 1.0)
            pushedOut = 0.0f;
        lastPushAt = clockSeconds;
        pushedOut -= octaves;
        if (pushedOut >= 1.5f)
        {
            pushedOut = 0.0f;
            onStepBack();
        }
        return;
    }
    pushedOut = 0.0f;
    setZoom (zoomNow * std::pow (2.0f, octaves), e.position);
}

void Specimen::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (! rail.contains (e.position))
        setZoom (zoomNow * scaleFactor, e.position);
}
} // namespace dendrite::ui
