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

    // the tracts: each neuron's three strongest outputs, by synapse count (inhibitory ones too)
    tractTo.assign ((size_t) neurons * tractsPerNeuron, 0);
    tractWeight.assign ((size_t) neurons * tractsPerNeuron, 0);
    for (int n = 0; n < neurons; ++n)
    {
        int best[tractsPerNeuron] = {}, bestAbs[tractsPerNeuron] = {};
        for (uint32_t k = flydata::rowStart[n]; k < flydata::rowStart[n + 1]; ++k)
        {
            const int w = std::abs ((int) flydata::weight[k]);
            for (int j = 0; j < tractsPerNeuron; ++j)
                if (w > bestAbs[j])
                {
                    for (int i = tractsPerNeuron - 1; i > j; --i)
                    {
                        best[i] = best[i - 1];
                        bestAbs[i] = bestAbs[i - 1];
                    }
                    best[j] = (int) k;
                    bestAbs[j] = w;
                    break;
                }
        }
        for (int j = 0; j < tractsPerNeuron; ++j)
            if (bestAbs[j] > 0)
            {
                tractTo[(size_t) (n * tractsPerNeuron + j)] = flydata::target[best[j]];
                tractWeight[(size_t) (n * tractsPerNeuron + j)] = flydata::weight[best[j]];
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
    r.removeFromLeft (52.0f);
    r.removeFromRight (24.0f);
    // the rail: the imaging method, the 2-DG brain, the view
    rail = r.removeFromRight (150.0f).withTrimmedTop (44.0f).withTrimmedBottom (10.0f); // under the caption; the legend stops short of it
    r.removeFromRight (24.0f);
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

    if (method == M::deoxyglucose)
    {
        double at = 0.0;
        float most = 1.0f;
        const float* frame = lapsePicture (at, most);
        renderUptake (points, frame, most, false, 1.0f / (pixelsPerMicron() * px), toMicrons (brain.getTopLeft()));
        return;
    }

    const float most = (float) std::log1p ((double) std::max<uint32_t> (1u, rec.mostExposed()));
    // one logical pixel at the default window (816 um across ~790 px: about 1 um a pixel), growing
    // with the window so a large picture keeps its density; always whole physical pixels
    // magnified a neuron grows with the square root of the magnification (a soma is a few um across)
    const int dot = std::max (1, juce::roundToInt (px * std::max (1.0f, brain.getWidth() / 790.0f) * std::sqrt (std::max (1.0f, zoomNow))));

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
    const float fullCalcium = rig::Recording::calciumAt (50.0f);

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
                // GCaMP6s: a calcium kernel on the spikes; grey from the indicator's floor to white at 50 Hz held
                const float c = rec.calcium (n);
                put (x0, y0, grey (cue (n, tone::floor + (tone::live - tone::floor) * juce::jlimit (0.0f, 1.0f, std::sqrt (c / fullCalcium)))), dot, false);
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
                // the neurons only as a faint ground; the lines are drawn over them (paintTracts)
                put (x0, y0, grey (cue (n, tone::floor + exposed * 0.5f)), dot, false);
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

void Specimen::renderUptake (juce::Image& into, const float* uptake, float most, bool front, float umPerPixel, juce::Point<float> origin)
{
    // the film: each neuron's uptake laid down where it is, blurred to tritium's few microns, the
    // densest neuron of the session white, on a log scale
    const int w = into.getWidth(), h = into.getHeight();
    scratch.assign ((size_t) (w * h), 0.0f);
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
        scratch[(size_t) (y * w + x)] += (float) std::log1p (u);
    }
    const int r = std::max (1, juce::roundToInt (4.0f / umPerPixel));
    std::vector<float> row ((size_t) std::max (w, h));
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int y = 0; y < h; ++y)
        {
            float* line = scratch.data() + y * w;
            std::copy (line, line + w, row.begin());
            float sum = 0.0f;
            for (int x = -r; x < w + r; ++x)
            {
                if (x + r < w) sum += row[(size_t) (x + r)];
                if (x - r - 1 >= 0) sum -= row[(size_t) (x - r - 1)];
                if (x >= 0 && x < w) line[x] = sum / (float) (2 * r + 1);
            }
        }
        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y) row[(size_t) y] = scratch[(size_t) (y * w + x)];
            float sum = 0.0f;
            for (int y = -r; y < h + r; ++y)
            {
                if (y + r < h) sum += row[(size_t) (y + r)];
                if (y - r - 1 >= 0) sum -= row[(size_t) (y - r - 1)];
                if (y >= 0 && y < h) scratch[(size_t) (y * w + x)] = sum / (float) (2 * r + 1);
            }
        }
    }
    float peak = 1.0e-6f;
    for (float v : scratch) peak = std::max (peak, v);
    // the same white for the same uptake through the whole time-lapse: scaled to the session's most
    const float ref = std::max (peak, (float) std::log1p (most) / (float) ((2 * r + 1) * (2 * r + 1)) * 4.0f);
    juce::Image::BitmapData bits (into, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float v = scratch[(size_t) (y * w + x)] / ref;
            const auto g = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * std::pow (juce::jlimit (0.0f, 1.0f, v), 0.6f)));
            auto* p = bits.getPixelPointer (x, y);
            p[0] = p[1] = p[2] = g;
        }
}

const float* Specimen::lapsePicture (double& atSeconds, float& most)
{
    // the time-lapse plays the session's keyframes over lapsePlaySeconds, then holds on now
    const auto& rec = processor.recording;
    most = std::max (1.0f, rec.mostUptake());
    const int frames = rec.lapseFrames();
    atSeconds = rec.sessionSeconds();
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
    return rec.lapseFrame (k);
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
    if (processor.recording.method.load() == rig::Recording::tracts)
        paintTracts (g, line);
    hovered = findHover();
    paintScale (g, line);
    if (frontal() && std::abs (zoomNow - 1.0f) < 0.01f)
        paintRegions (g, line);
    measureLight();
    paintSites (g, line);
    if (! noElectrode)
        paintLight (g, line);
    paintHover (g, line);
    paintRail (g, line);
}

int Specimen::findHover() const
{
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
        return;
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
    g.drawText (text, juce::Rectangle<float> (14.0f, 24.0f, (float) getWidth() - 28.0f, 16.0f), juce::Justification::centredLeft, true);
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
    const float fullCalcium = rig::Recording::calciumAt (50.0f);
    auto calciumLevel = [&] (float hz) { return tone::floor + (tone::live - tone::floor) * std::sqrt (rig::Recording::calciumAt (hz) / fullCalcium); };
    switch (method)
    {
        case M::twoPhoton: levels = { { tone::floor, "no calcium" }, { calciumLevel (2.0f), "2 Hz held" }, { calciumLevel (10.0f), "10 Hz" }, { tone::live, utf8 ("\xe2\x89\xa5 50 Hz") } }; break;
        case M::voltage: levels = { { tone::mid, "at rest" }, { tone::floor, "spiked this frame" }, { 0.04f, utf8 ("\xe2\x89\xa5 2 spikes") } }; break;
        case M::photonCounting: levels = { { 150.0f / 255.0f, "1 photon = 1 spike, last 1 s" } }; break;
        case M::tracts: levels = { { tone::bright, "excitatory" }, { tone::mid, "inhibitory" } }; break;
        case M::deoxyglucose: levels = { { tone::dim, "little uptake" }, { tone::live, "most this session" } }; break;
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
        x0 = std::max (14.0f, std::min (left, rail.getX() - 24.0f - width));
    }
    g.setColour (tone::grey (tone::bright));
    line.h (x0, x0 + len, y);
    line.v (x0, y - 3.0f, y + 3.0f);
    line.v (x0 + len, y - 3.0f, y + 3.0f);
    g.setFont (fonts->at (type::data));
    g.drawText (microns (um), juce::Rectangle<float> (x0 + len + 8.0f, y - 8.0f, 80.0f, 16.0f), juce::Justification::centredLeft);

    {
        g.setFont (fonts->at (type::small));
        float x = x0 + len + 90.0f;
        for (const auto& sw : levels)
        {
            g.setColour (tone::grey (tone::floor));
            g.drawRect (juce::Rectangle<float> (x - 1.0f, y - 4.0f, 8.0f, 8.0f), line.width()); // a frame, so a dark step still reads
            g.setColour (tone::grey (sw.level));
            g.fillRect (juce::Rectangle<float> (x, y - 3.0f, 6.0f, 6.0f));
            g.setColour (tone::grey (tone::mid));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), sw.label);
            g.drawText (sw.label, juce::Rectangle<float> (x + 10.0f, y - 8.0f, w + 2.0f, 16.0f), juce::Justification::centredLeft);
            x += 10.0f + w + 14.0f;
        }
        if (showScars)
        {
            // a scar's mark, as the map draws it, settled
            g.setColour (tone::grey (tone::mid));
            g.fillRect (juce::Rectangle<float> (x, y - 0.5f, 7.0f, 1.0f));
            g.fillRect (juce::Rectangle<float> (x + 3.0f, y - 3.5f, 1.0f, 7.0f));
            g.drawText (scars, juce::Rectangle<float> (x + 11.0f, y - 8.0f, 90.0f, 16.0f), juce::Justification::centredLeft);
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
        case M::twoPhoton: how = "two-photon" + dot + "gcamp6s: rises 0.1 s, decays 0.8 s"; break;
        case M::voltage: how = "voltage" + dot + utf8 ("asap2f: 2.5 ms, so a frame holds only its own spikes"); break;
        case M::photonCounting: how = "photon counting" + dot + utf8 ("gfp-aequorin: grains within 3 \xc2\xb5m"); break;
        case M::tracts: how = "tractography" + dot + "the 3 strongest outputs of each neuron firing now"; break;
        case M::deoxyglucose:
        {
            double at = 0.0;
            float most = 0.0f;
            lapsePicture (at, most);
            how = utf8 ("\xc2\xb3h-2-deoxyglucose") + dot + "this session only" + dot + clock (at).fromFirstOccurrenceOf ("+", false, false);
            if (lapseStarted >= 0.0 && clockNow - lapseStarted < lapsePlaySeconds)
                how << " of " << clock (processor.recording.sessionSeconds()).fromFirstOccurrenceOf ("+", false, false);
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
    g.drawText (counts, juce::Rectangle<float> (14.0f, 8.0f, rail.getX() - 28.0f, 16.0f), juce::Justification::centredLeft);
    if (hovered < 0)
        g.drawText (what, juce::Rectangle<float> (14.0f, 24.0f, rail.getX() - 28.0f, 16.0f), juce::Justification::centredLeft, true);
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
            g.setColour (tone::grey (tone::mid));
            g.drawText (p.text, juce::Rectangle<float> (p.x, textY - 7.0f, p.w + 2.0f, 14.0f), juce::Justification::centredLeft);
        }
    }
}

//==============================================================================
// the tracts and the rail

void Specimen::paintTracts (juce::Graphics& g, const Hairline& line)
{
    // every neuron firing at 5 Hz or more, busiest first (at most 600), drawn along its three
    // strongest outputs; a line is as bright as its neuron fires
    const auto& rec = processor.recording;
    std::vector<std::pair<float, int>> firing;
    for (int n = 0; n < neurons; ++n)
    {
        const float hz = rec.trace (n) / (float) rig::Recording::traceSeconds;
        if (hz >= 5.0f)
            firing.push_back ({ hz, n });
    }
    const size_t most = std::min<size_t> (firing.size(), 600);
    std::partial_sort (firing.begin(), firing.begin() + (long) most, firing.end(), [] (auto a, auto b) { return a.first > b.first; });
    juce::Graphics::ScopedSaveState keep (g);
    g.reduceClipRegion (brain.toNearestInt());
    for (size_t k = most; k-- > 0;) // the busiest last, on top
    {
        const auto [hz, n] = firing[k];
        const float strength = juce::jlimit (0.0f, 1.0f, std::log10 (hz / 5.0f) / std::log10 (40.0f));
        for (int j = 0; j < tractsPerNeuron; ++j)
        {
            const int w = tractWeight[(size_t) (n * tractsPerNeuron + j)];
            if (w == 0)
                continue;
            const float base = w > 0 ? tone::bright : tone::mid;
            g.setColour (tone::grey (base).withAlpha (0.12f + 0.55f * strength));
            g.drawLine (juce::Line<float> (proj[(size_t) n], proj[(size_t) tractTo[(size_t) (n * tractsPerNeuron + j)]]), line.width());
        }
    }
}

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
        renderUptake (inset, now.data(), std::max (1.0f, rec.mostUptake()), true, widthMicrons / (float) w);
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
    }

    // the view: how the brain is turned and magnified, and how to change it (no dial: the brain is the handle)
    // in a short window the legend row under the map runs under the rail: stop above it
    const float floor = std::min (rail.getBottom(), brain.getBottom() + 44.0f);
    if (insetArea.getBottom() + 34.0f + 22.0f + railRow <= floor)
    {
        const float y = insetArea.getBottom() + 34.0f;
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
        juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (brain.toNearestInt());
        // the light itself: a soft disc, brighter while it flashes
        juce::ColourGradient glow (juce::Colours::white.withAlpha (flash ? 0.30f : 0.09f), at.x, at.y,
                                   juce::Colours::white.withAlpha (0.0f), at.x + r * 1.08f, at.y, true);
        glow.addColour (0.75, juce::Colours::white.withAlpha (flash ? 0.18f : 0.05f));
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (2.16f * r, 2.16f * r).withCentre (at));
    }

    // the stimulus: a hairline from where the hand presses to the spot, and the press site
    if (touching)
    {
        g.setColour (tone::grey (tone::bright));
        const auto toward = at + (pressAt - at) * (r / std::max (r, pressAt.getDistanceFrom (at)));
        g.drawLine (juce::Line<float> (pressAt, toward), line.width());
        g.drawRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ line.snap (pressAt.x), line.snap (pressAt.y) }), line.width());
    }

    // its edge (the size: space), the power around it (feed), from 7:30 round to 4:30
    const auto centre = juce::Point<float> (line.snap (at.x), line.snap (at.y));
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
    // the centre, to move it
    g.setColour (tone::grey (dragPart == Part::centre || hoverPart == Part::centre ? tone::live : tone::bright));
    g.drawRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre (centre), line.width());

    // what it is, beside it: size, lit, power, the flash's rate
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

    // where it sits, marked on the calibration axes
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
    const auto dot = utf8 (" \xc2\xb7 ");
    if (methodAt (hoverAt) >= 0)
        return "imaging: what the plate shows of the same spikes";
    if (insetArea.expanded (2.0f).contains (hoverAt))
        return "2-dg: every spike of this session only, never saved" + dot + "press: play how it built up";
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
    setZoom (zoomNow * std::pow (2.0f, juce::jlimit (-1.0f, 1.0f, sign * delta * 2.0f)), e.position);
}

void Specimen::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (! rail.contains (e.position))
        setZoom (zoomNow * scaleFactor, e.position);
}
} // namespace dendrite::ui
