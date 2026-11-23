#include "Timeline.h"

#include <algorithm>
#include <cmath>

namespace dendrite::rig
{
void Timeline::setSampleRate (double rate) noexcept
{
    if (rate <= 0.0 || std::abs (rate - sampleRate) < 1.0e-6)
        return;
    sampleRate = rate;
    binSamples = std::max (1, (int) std::lround (rate * binSeconds));
}

const Timeline::Bin& Timeline::bin (int back) const noexcept
{
    return ring[(size_t) ((head - 1 - back + binCount * 2) % binCount)];
}

double Timeline::binTime (int back) const noexcept
{
    return sampleRate > 0.0 ? (double) (finishedBins - 1 - back) * (double) binSamples / sampleRate : 0.0;
}

const char* Timeline::moveName (int move) noexcept
{
    static const char* names[] = { "none", "hop", "leap", "duck", "dive", "rush", "hold", "sink" };
    return names[std::clamp (move, 0, 7)];
}

void Timeline::finishBin() noexcept
{
    ring[(size_t) head] = current;
    head = (head + 1) % binCount;
    filled = std::min (filled + 1, binCount);
    ++finishedBins;
    current = {};
    currentEmpty = true;
    inBin = 0;
}

Timeline::Reflex& Timeline::newReflex() noexcept
{
    auto& r = reflexRing[(size_t) reflexHead];
    r = {};
    reflexHead = (reflexHead + 1) % eventCount;
    reflexCount = std::min (reflexCount + 1, eventCount);
    return r;
}

Timeline::Span& Timeline::newSpan (std::array<Span, eventCount>& spans, int& at, int& count) noexcept
{
    auto& s = spans[(size_t) at];
    s = {};
    at = (at + 1) % eventCount;
    count = std::min (count + 1, eventCount);
    return s;
}

void Timeline::add (const Frame& f) noexcept
{
    if (sampleRate <= 0.0 || f.samples <= 0)
        return;
    const double t = now();
    const double seconds = (double) f.samples / sampleRate;
    lastBpm = f.bpm;
    lastBarBeats = f.barBeats;
    lastPlaying = f.playing;

    // events, at the frame's start: the same edges the rig's log uses
    if (f.jump > 0.01f && ! jumping)
    {
        jumping = true;
        auto& r = newReflex();
        r.at = t;
        r.beat = f.beat;
        r.move = f.move;
        r.open = true;
        r.playing = f.playing;
    }
    if (jumping && reflexCount > 0)
    {
        auto& r = reflexRing[(size_t) ((reflexHead - 1 + eventCount) % eventCount)];
        if (std::abs (f.semitones) > 0.05f && (r.steps == 0 || std::abs (f.semitones - r.semitones[(size_t) (r.steps - 1)]) > 0.05f)
            && r.steps < (int) r.semitones.size())
            r.semitones[(size_t) r.steps++] = f.semitones;
        if (f.move != 0)
            r.move = f.move;
        r.length = t + seconds - r.at;
        if (f.jump <= 0.01f)
        {
            jumping = false;
            r.open = false;
        }
    }
    if (f.scratch > 0.3f && ! grooming)
    {
        grooming = true;
        auto& s = newSpan (groomRing, groomHead, groomCount);
        s.at = t;
        s.hz = f.scratchHz;
        s.open = true;
    }
    if (grooming)
    {
        auto& s = groomRing[(size_t) ((groomHead - 1 + eventCount) % eventCount)];
        s.length = t + seconds - s.at;
        if (f.scratch < 0.08f)
        {
            grooming = false;
            s.open = false;
        }
    }
    if (f.thrown && ! throwing)
    {
        throwing = true;
        auto& s = newSpan (throwRing, throwHead, throwCount);
        s.at = t;
        s.open = true;
    }
    if (throwing)
    {
        auto& s = throwRing[(size_t) ((throwHead - 1 + eventCount) % eventCount)];
        s.length = t + seconds - s.at;
        if (! f.thrown)
        {
            throwing = false;
            s.open = false;
        }
    }

    // the bins: a frame longer than a bin fills each bin it covers
    const double beatsPerSample = f.bpm / 60.0 / sampleRate;
    int left = f.samples, done = 0;
    while (left > 0)
    {
        if (currentEmpty)
        {
            current.beat = f.beat + done * beatsPerSample;
            current.playing = f.playing;
            currentEmpty = false;
        }
        current.in = std::max (current.in, f.in);
        current.wet = std::max (current.wet, f.wet);
        current.jump = std::max (current.jump, f.jump);
        current.scratch = std::max (current.scratch, f.scratch);
        if (std::abs (f.semitones) >= std::abs (current.semitones))
            current.semitones = f.semitones;
        current.turn = f.turn;
        current.thrown = current.thrown || f.thrown;
        const int take = std::min (left, binSamples - inBin);
        inBin += take;
        left -= take;
        done += take;
        if (inBin >= binSamples)
            finishBin();
    }
    samplesSeen += f.samples;
}
} // namespace dendrite::rig
