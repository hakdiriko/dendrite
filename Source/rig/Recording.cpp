#include "Recording.h"
#include <numeric>

namespace dendrite::rig
{
const char* Recording::populationName (int p) noexcept
{
    static const char* names[] = { "ears l", "ears r", "eyes l", "eyes r", "smell", "kc", "mbon", "ppl1", "pam", "steer l", "steer r", "groom", "giant fiber" };
    return p >= 0 && p < populations ? names[p] : "";
}

const char* Recording::methodName (int m) noexcept
{
    static const char* names[] = { "trace", "two-photon", "voltage", "photons", "tracts", "2-dg" };
    return m >= 0 && m < methods ? names[m] : "";
}

Recording::Recording()
    : caFast ((size_t) neurons, 0.0f), caSlow ((size_t) neurons, 0.0f), uptakes ((size_t) neurons, 0.0f),
      photonRing ((size_t) neurons * 128, 0), photonSum ((size_t) neurons, 0),
      scarCount ((size_t) neurons, 0), scarLast ((size_t) neurons, 0.0), order ((size_t) neurons, 0),
      incoming ((size_t) neurons, 0), lastTick ((size_t) neurons, 0), traces ((size_t) neurons, 0.0f), exposed ((size_t) neurons, 0u)
{
    using namespace flydata;
    // the senses are B, A, CE, loom low, mid, high, each left then right; then the 32 smell bands
    auto senses = [] (std::vector<uint16_t>& into, int first, int final, int stride) {
        for (int s = first; s <= final; s += stride)
            for (int k = senseStart[s]; k < senseStart[s + 1]; ++k)
                into.push_back (senseNeuron[k]);
    };
    senses (members[earsLeft], 0, 4, 2);
    senses (members[earsRight], 1, 5, 2);
    senses (members[eyesLeft], 6, 10, 2);
    senses (members[eyesRight], 7, 11, 2);
    senses (members[smell], flydata::senses, flydata::senses + smells - 1, 1);
    members[Population::kenyon].assign (std::begin (flydata::kenyon), std::end (flydata::kenyon));
    members[Population::mbon].assign (std::begin (flydata::mbon), std::end (flydata::mbon));
    members[Population::ppl1].assign (std::begin (flydata::ppl1), std::end (flydata::ppl1));
    members[Population::pam].assign (std::begin (flydata::pam), std::end (flydata::pam));
    members[Population::steerLeft].assign (std::begin (flydata::steerLeft), std::end (flydata::steerLeft));
    members[Population::steerRight].assign (std::begin (flydata::steerRight), std::end (flydata::steerRight));
    members[groom].assign (std::begin (legNeuron), std::end (legNeuron));
    members[giantFiber].assign (std::begin (escapeNeuron), std::end (escapeNeuron));
    for (auto& m : members) // a neuron can sit in more than one sense; count it once per population
    {
        std::sort (m.begin(), m.end());
        m.erase (std::unique (m.begin(), m.end()), m.end());
    }
}

uint32_t Recording::log (const juce::String& text, uint32_t mark)
{
    Event e;
    e.id = nextEvent++;
    e.at = sessionSeconds();
    e.text = text;
    e.mark = mark;
    eventList.push_back (std::move (e));
    while ((int) eventList.size() > logLength)
        eventList.pop_front();
    return eventList.back().id;
}

Recording::Event* Recording::event (uint32_t id) noexcept
{
    for (auto it = eventList.rbegin(); it != eventList.rend(); ++it)
        if (it->id == id)
            return &*it;
    return nullptr;
}

void Recording::scar()
{
    // the hardest firing over the last 300 ms (each neuron's trace), at the moment the reflex reached the rig
    std::iota (order.begin(), order.end(), 0);
    std::nth_element (order.begin(), order.begin() + scarNeurons, order.end(), [this] (int a, int b) { return traces[(size_t) a] > traces[(size_t) b]; });
    const double t = lived();
    const juce::SpinLock::ScopedLockType hold (lock);
    for (int k = 0; k < scarNeurons; ++k)
    {
        const auto n = (size_t) order[(size_t) k];
        if (traces[n] <= 0.0f)
            continue; // a silent neuron did not drive it
        scarCount[n] = (uint16_t) std::min (65535, scarCount[n] + 1);
        scarLast[n] = t;
    }
    ++scarsMade;
}

void Recording::noteState (const std::array<float, quantities>& state)
{
    // once a second of the session's clock; a gap (the timer stalled) is not filled in
    if (lastNoted >= 0.0 && now - lastNoted < 1.0)
        return;
    lastNoted = lastNoted < 0.0 || now - lastNoted > 2.0 ? now : lastNoted + 1.0;
    for (int q = 0; q < quantities; ++q)
        historyData[(size_t) (historyHead * quantities + q)] = state[(size_t) q];
    historyHead = (historyHead + 1) % historySeconds;
    historyCount = std::min (historyCount + 1, historySeconds);
}

void Recording::step (double nowSeconds)
{
    if (started < 0.0)
        started = nowSeconds;
    now = nowSeconds;
    const double dt = last < 0.0 ? 0.0 : std::max (0.0, nowSeconds - last);
    last = nowSeconds;
    const auto keep = (float) std::exp (-dt / traceSeconds);
    const auto keepRise = (float) std::exp (-dt / calciumRise), keepDecay = (float) std::exp (-dt / calciumDecay);
    frameDt = dt;
    // photon counting: the oldest ticks leave the one-second window, this one enters it
    while (photonTail != photonHead && photonAt[(size_t) photonTail] < nowSeconds - photonSeconds)
    {
        const uint16_t* old = photonRing.data() + (size_t) photonTail * (size_t) neurons;
        for (size_t n = 0; n < (size_t) neurons; ++n)
            photonSum[n] = (uint16_t) (photonSum[n] - old[n]);
        photonTail = (photonTail + 1) % 128;
    }
    if ((photonHead + 1) % 128 == photonTail) // full (a timer far faster than 30 Hz): drop the oldest early
    {
        const uint16_t* old = photonRing.data() + (size_t) photonTail * (size_t) neurons;
        for (size_t n = 0; n < (size_t) neurons; ++n)
            photonSum[n] = (uint16_t) (photonSum[n] - old[n]);
        photonTail = (photonTail + 1) % 128;
    }
    uint16_t* slot = photonRing.data() + (size_t) photonHead * (size_t) neurons;
    photonAt[(size_t) photonHead] = nowSeconds;
    photonHead = (photonHead + 1) % 128;

    juce::int64 total = 0;
    {
        const juce::SpinLock::ScopedLockType hold (lock);
        for (size_t n = 0; n < (size_t) neurons; ++n)
        {
            const uint16_t c = incoming[n];
            incoming[n] = 0;
            lastTick[n] = c;
            traces[n] = traces[n] * keep + (float) c;
            caFast[n] = caFast[n] * keepRise + (float) c;
            caSlow[n] = caSlow[n] * keepDecay + (float) c;
            slot[n] = c;
            photonSum[n] = (uint16_t) std::min (65535, photonSum[n] + c);
            if (c != 0)
            {
                exposed[n] += c;
                exposedMax = std::max (exposedMax, exposed[n]);
                uptakes[n] += (float) c;
                uptakeMax = std::max (uptakeMax, uptakes[n]);
                total += c;
            }
        }
    }
    sessionSpikes += total;

    // the 2-DG time-lapse: a keyframe every lapseEvery seconds; when the reel is full, every other
    // frame goes and the interval doubles, so the whole session always fits in lapseLength frames
    if (lapseAt.empty() || sessionSeconds() - lapseAt.back() >= lapseEvery)
    {
        if ((int) lapseAt.size() >= lapseLength)
        {
            for (size_t k = 1; k < lapseAt.size() / 2 + (lapseAt.size() & 1); ++k)
            {
                lapse[k] = std::move (lapse[2 * k]);
                lapseAt[k] = lapseAt[2 * k];
            }
            const size_t kept = lapseAt.size() / 2 + (lapseAt.size() & 1);
            lapse.resize (kept);
            lapseAt.resize (kept);
            lapseEvery *= 2.0;
        }
        lapse.push_back (uptakes);
        lapseAt.push_back (sessionSeconds());
    }

    // the raster: each population's rate this tick (none while the specimen's time stands still)
    if (dt > 0.0 && stepping)
    {
        float* row = rasterData.data() + rasterHead * populations;
        for (int p = 0; p < populations; ++p)
        {
            int sum = 0;
            for (auto n : members[(size_t) p])
                sum += lastTick[n];
            row[p] = (float) ((double) sum / (dt * (double) std::max<size_t> (1, members[(size_t) p].size())));
        }
        rasterAt[(size_t) rasterHead] = sessionSeconds();
        rasterHead = (rasterHead + 1) % rasterTicks;
        rasterCount = std::min (rasterCount + 1, rasterTicks);
    }
    if (dt > 0.0)
    {
        // spikes per second over the whole cut, smoothed over about the trace's time
        const float r = (float) ((double) total / dt), a = (float) (1.0 - keep);
        rate += a * (r - rate);
    }
}

juce::ValueTree Recording::save() const
{
    juce::ValueTree rig (tag);
    rig.setProperty ("version", version, nullptr);
    juce::MemoryOutputStream packed;
    {
        juce::GZIPCompressorOutputStream zip (packed, 9);
        const juce::SpinLock::ScopedLockType hold (lock);
        for (auto e : exposed)
            zip.writeInt ((int) e); // little-endian, every platform
        zip.flush();
    }
    rig.setProperty ("neurons", neurons, nullptr);
    {
        // scars: per neuron its count, then when its last one was made (seconds of the set's history)
        juce::MemoryOutputStream scarsPacked;
        {
            juce::GZIPCompressorOutputStream zip (scarsPacked, 9);
            const juce::SpinLock::ScopedLockType hold (lock);
            for (size_t n = 0; n < (size_t) neurons; ++n)
            {
                zip.writeShort ((short) scarCount[n]);
                zip.writeFloat ((float) scarLast[n]);
            }
            zip.flush();
        }
        rig.setProperty ("scars", juce::Base64::toBase64 (scarsPacked.getData(), scarsPacked.getDataSize()), nullptr);
        rig.setProperty ("scarsMade", scarsMade, nullptr);
    }
    rig.setProperty ("exposure", juce::Base64::toBase64 (packed.getData(), packed.getDataSize()), nullptr);
    // the history this set carries: what it came with plus this session
    rig.setProperty ("seconds", savedLived + std::max (0.0, now - started), nullptr);
    // how it was being looked at (3.2)
    rig.setProperty ("imaging", juce::String (methodName (method.load())), nullptr);
    rig.setProperty ("yaw", yaw.load(), nullptr);
    rig.setProperty ("pitch", pitch.load(), nullptr);
    rig.setProperty ("zoom", zoom.load(), nullptr);
    rig.setProperty ("zoomX", zoomX.load(), nullptr);
    rig.setProperty ("zoomY", zoomY.load(), nullptr);
    rig.setProperty ("lightX", lightX.load(), nullptr);
    rig.setProperty ("lightY", lightY.load(), nullptr);
    return rig;
}

void Recording::load (const juce::ValueTree& rig)
{
    // a later version adds fields; it never changes these, so anything from version 1 on loads
    if (! rig.hasType (tag) || (int) rig.getProperty ("version", 0) < 1 || (int) rig.getProperty ("neurons", 0) != neurons)
        return;
    // how it was being looked at (absent before 3.2: the frontal trace)
    {
        const auto name = rig.getProperty ("imaging", "trace").toString();
        for (int m = 0; m < methods; ++m)
            if (name == methodName (m))
                method = m;
        yaw = juce::jlimit (-180.0f, 180.0f, (float) rig.getProperty ("yaw", 0.0f));
        pitch = juce::jlimit (-90.0f, 90.0f, (float) rig.getProperty ("pitch", 0.0f));
        zoom = juce::jlimit (minZoom, maxZoom, (float) rig.getProperty ("zoom", 1.0f));
        zoomX = juce::jlimit (0.0f, 1.0f, (float) rig.getProperty ("zoomX", 0.5f));
        zoomY = juce::jlimit (0.0f, 1.0f, (float) rig.getProperty ("zoomY", 0.5f));
        lightX = juce::jlimit (0.0f, 1.0f, (float) rig.getProperty ("lightX", 0.5f));
        lightY = juce::jlimit (0.0f, 1.0f, (float) rig.getProperty ("lightY", 0.5f));
    }
    juce::MemoryOutputStream packed;
    if (! juce::Base64::convertFromBase64 (packed, rig.getProperty ("exposure").toString()))
        return;
    juce::MemoryInputStream raw (packed.getData(), packed.getDataSize(), false);
    juce::GZIPDecompressorInputStream unzip (raw);
    std::vector<uint32_t> back ((size_t) neurons, 0u);
    for (auto& e : back)
    {
        if (unzip.isExhausted())
            return; // broken: keep this session as it is
        e = (uint32_t) unzip.readInt();
    }
    const juce::SpinLock::ScopedLockType hold (lock);
    exposedMax = 0;
    for (size_t n = 0; n < back.size(); ++n)
    {
        exposed[n] = back[n];
        exposedMax = std::max (exposedMax, back[n]);
    }
    savedLived = restored = std::max (0.0, (double) rig.getProperty ("seconds", 0.0));

    // scars (absent in a set saved before they existed: it simply has none)
    juce::MemoryOutputStream scarsPacked;
    if (rig.hasProperty ("scars") && juce::Base64::convertFromBase64 (scarsPacked, rig.getProperty ("scars").toString()))
    {
        juce::MemoryInputStream scarsRaw (scarsPacked.getData(), scarsPacked.getDataSize(), false);
        juce::GZIPDecompressorInputStream scarsUnzip (scarsRaw);
        std::vector<uint16_t> count ((size_t) neurons, 0);
        std::vector<double> when ((size_t) neurons, 0.0);
        for (size_t n = 0; n < (size_t) neurons; ++n)
        {
            if (scarsUnzip.isExhausted())
                return;
            count[n] = (uint16_t) scarsUnzip.readShort();
            when[n] = (double) scarsUnzip.readFloat();
        }
        scarCount = std::move (count);
        scarLast = std::move (when);
        scarsMade = std::max (0, (int) rig.getProperty ("scarsMade", 0));
    }
}
} // namespace dendrite::rig
