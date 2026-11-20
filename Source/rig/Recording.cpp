#include "Recording.h"

namespace dendrite::rig
{
Recording::Recording()
    : incoming ((size_t) neurons, 0), traces ((size_t) neurons, 0.0f), exposed ((size_t) neurons, 0u)
{
}

void Recording::step (double nowSeconds)
{
    if (started < 0.0)
        started = nowSeconds;
    now = nowSeconds;
    const double dt = last < 0.0 ? 0.0 : std::max (0.0, nowSeconds - last);
    last = nowSeconds;
    const auto keep = (float) std::exp (-dt / traceSeconds);

    juce::int64 total = 0;
    {
        const juce::SpinLock::ScopedLockType hold (lock);
        for (size_t n = 0; n < (size_t) neurons; ++n)
        {
            const uint16_t c = incoming[n];
            incoming[n] = 0;
            traces[n] = traces[n] * keep + (float) c;
            if (c != 0)
            {
                exposed[n] += c;
                exposedMax = std::max (exposedMax, exposed[n]);
                total += c;
            }
        }
    }
    sessionSpikes += total;
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
    rig.setProperty ("exposure", juce::Base64::toBase64 (packed.getData(), packed.getDataSize()), nullptr);
    // the history this set carries: what it came with plus this session
    rig.setProperty ("seconds", savedLived + std::max (0.0, now - started), nullptr);
    return rig;
}

void Recording::load (const juce::ValueTree& rig)
{
    // a later version adds fields; it never changes these, so anything from version 1 on loads
    if (! rig.hasType (tag) || (int) rig.getProperty ("version", 0) < 1 || (int) rig.getProperty ("neurons", 0) != neurons)
        return;
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
}
} // namespace dendrite::rig
