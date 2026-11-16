// The fly's body, played by a hand (2.5, phase 2 of the plan "The fly: second review and plan").
//
// The fly's brain says when and how hard; its body (Fly::startMove and the musical half of
// Fly::reflexes) turns that into a move on the song's grid: the tape heads slide in straight
// lines, so every slide bends the repeats by an exact interval for an exact note value. This
// file is that body without the brain, so a person can play the same moves by hand on the teach
// page and the offline replay. What the person plays is the fly's teaching data.
//
// The shapes below are copied from Fly::startMove on 2.4.3 with the brain's choices fixed:
// rush runs double time for a beat (the fly does a half bar when wound up), dive stays down for
// a half note (a bar when very scared), duck never freezes (the fly does after a hard hit). The
// fly itself still uses its own copy; phase 4 makes the fly use this body, so there is one.
//
// No JUCE, no allocation, real-time safe. Driven per sample.
#pragma once

#include "Hand.h"
#include "Tools.h"

#include <array>
#include <cmath>

namespace dendrite
{
class FlyBody
{
public:
    // same numbers as Fly::Move
    enum Move { none, hop, leap, duck, dive, rush, hold, sink, moves };
    static constexpr const char* moveNames[moves] = { "none", "hop", "leap", "duck", "dive", "rush", "hold", "sink" };

    struct Leg
    {
        float slope = 0.0f, left = 0.0f, end = 1.0f; // per 16th: how the heads' distance changes, for how long, where it ends
    };
    struct Shape
    {
        std::array<Leg, 3> legs {};
        int count = 0;
        float throw16 = 0.0f;     // it throws the moment into the echo for this many 16ths
        float freezeIn16 = -1.0f; // and then holds its breath for this many 16ths, from the next beat (-1: no)
        bool big = false;         // big moves wait for the half bar, small ones for the next 16th
    };

    static Shape shape (int move) noexcept
    {
        Shape s;
        const auto leg = [&s] (int k, float slope, float sixteenths, float end) { s.legs[(size_t) k] = { slope, sixteenths, end }; };
        switch (move)
        {
            case hop: // up an octave for a 16th, back over an 8th
                leg (0, -0.25f, 1.0f, 0.75f);
                leg (1, 0.125f, 2.0f, 1.0f);
                s.count = 2;
                s.throw16 = 1.0f;
                break;
            case leap: // up an octave and a fifth for a 16th, back down through an octave over a dotted 8th
                leg (0, -0.5f, 1.0f, 0.5f);
                leg (1, 1.0f / 6.0f, 3.0f, 1.0f);
                s.count = 2;
                s.throw16 = 2.0f;
                s.big = true;
                break;
            case duck: // down an octave for an 8th, back up in a 16th
                leg (0, 0.125f, 2.0f, 1.25f);
                leg (1, -0.25f, 1.0f, 1.0f);
                s.count = 2;
                s.throw16 = 2.0f;
                break;
            case dive: // down an octave over a half note into half time, stays there, climbs back over a quarter
                leg (0, 0.125f, 8.0f, 2.0f);
                leg (1, 0.0f, 8.0f, 2.0f);
                leg (2, -0.25f, 4.0f, 1.0f);
                s.count = 3;
                s.throw16 = 3.0f;
                s.freezeIn16 = 4.0f;
                s.big = true;
                break;
            case rush: // up an octave for an 8th, double time for a beat, falls back over a quarter
                leg (0, -0.25f, 2.0f, 0.5f);
                leg (1, 0.0f, 4.0f, 0.5f);
                leg (2, 0.125f, 4.0f, 1.0f);
                s.count = 3;
                s.throw16 = 2.0f;
                s.big = true;
                break;
            case hold: // no tape at all: a throw and a held breath
                leg (0, 0.0f, 6.0f, 1.0f);
                s.count = 1;
                s.throw16 = 3.0f;
                s.freezeIn16 = 4.0f;
                break;
            case sink: // both heads fall by pure intervals for an 8th, then snap back in a 32nd
                leg (0, 1.0f / 6.0f, 2.0f, 4.0f / 3.0f);
                leg (1, -2.0f / 3.0f, 0.5f, 1.0f);
                s.count = 2;
                s.throw16 = 2.0f;
                break;
            default:
                break;
        }
        return s;
    }

    void prepare (float sampleRate) noexcept
    {
        sr = sampleRate;
        glide = approach (0.004f, sr);
        freezeGlide = approach (0.02f, sr);
        throwRise = approach (0.3f, sr);
        reset();
    }

    void reset() noexcept
    {
        legAt = legCount = 0;
        moveNow = none;
        cued = none;
        stretch = 1.0f;
        throw16 = freeze16 = 0.0f;
        freezeIn16 = -1.0f;
        throwHeld = freezeHeld = false;
        frozenByHand = false;
        throwGlow = freezeTarget = 0.0f;
        lastTick = -1;
        started = none;
        touch = Touch {};
    }

    // The song's clock, before each sample (or each short run of samples): the beat in quarter notes.
    void setClock (double beat, double bpm, double barBeats) noexcept
    {
        beatNow = beat;
        bpmNow = bpm;
        barNow = barBeats;
    }

    // A move, from the hand. It starts on the next line it may (a 16th, or the half bar for a
    // big one), and only when the last move is over. A second cue while one waits replaces it.
    void cue (int move) noexcept
    {
        if (move > none && move < moves)
            cued = move;
    }
    // Held keys: throw throws while held (at once, like dendrite's throw button), freeze holds the
    // loop from the next 16th until the 16th after it is let go.
    void setThrow (bool held) noexcept { throwHeld = held; }
    void setFreeze (bool held) noexcept { freezeHeld = held; }

    // Per sample. Returns the touch for the echo.
    const Touch& process() noexcept
    {
        const double pos = beatNow * 4.0;
        const long long tickNow = (long long) std::floor (pos);
        const bool tick = tickNow != lastTick;
        lastTick = tickNow;
        const float per16 = (float) (bpmNow / 60.0 * 4.0) / sr;
        if (tick)
        {
            const long long bar16 = std::max (4LL, (long long) std::llround (4.0 * barNow));
            const long long half = std::max (4LL, bar16 / 2);
            if (cued != none && legAt >= legCount)
            {
                const Shape s = shape (cued);
                if (tickNow % (s.big ? half : 1) == 0)
                {
                    legs = s.legs;
                    legCount = s.count;
                    legAt = 0;
                    stretch = 1.0f;
                    throw16 = std::max (throw16, s.throw16);
                    if (s.freezeIn16 >= 0.0f)
                        freezeIn16 = s.freezeIn16;
                    moveNow = cued;
                    started = cued;
                    startedBeat = (double) tickNow / 4.0;
                    cued = none;
                }
            }
            if (tickNow % 4 == 0 && freezeIn16 >= 0.0f && freeze16 <= 0.0f)
            {
                freeze16 = freezeIn16;
                freezeIn16 = -1.0f;
            }
            frozenByHand = freezeHeld;
        }
        if (legAt < legCount)
        {
            auto& leg = legs[(size_t) legAt];
            stretch += leg.slope * per16;
            leg.left -= per16;
            if (leg.left <= 0.0f)
            {
                stretch = leg.end;
                ++legAt;
            }
        }
        else
        {
            stretch = 1.0f;
            moveNow = none;
        }
        const bool throwing = throw16 > 0.0f || throwHeld;
        throw16 -= per16;
        const bool freezing = freeze16 > 0.0f || frozenByHand;
        freeze16 -= per16;

        throwGlow += throwRise * ((throwing ? 1.0f : 0.0f) - throwGlow);
        freezeTarget += freezeGlide * ((freezing ? 1.0f : 0.0f) - freezeTarget);
        const float speed = 1.0f / std::clamp (stretch, 0.45f, 2.2f);
        const float lift = 1.0f + 0.8f * throwGlow;
        touch.speed += glide * (speed - touch.speed);
        touch.freeze += glide * (freezeTarget - touch.freeze);
        touch.lift += glide * (lift - touch.lift);
        touch.throwing = throwing;
        return touch;
    }

    int moving() const noexcept { return moveNow; }
    int waiting() const noexcept { return cued; }
    bool throwing() const noexcept { return touch.throwing; }
    bool frozen() const noexcept { return touch.freeze > 0.5f; }
    // The move that started since the last call, and on which beat (none if none did).
    int takeStarted (double& beat) noexcept
    {
        const int m = started;
        started = none;
        beat = startedBeat;
        return m;
    }

private:
    float sr = 48000.0f, glide = 0.005f, freezeGlide = 0.001f, throwRise = 0.0001f;
    double beatNow = 0.0, bpmNow = 120.0, barNow = 4.0, startedBeat = 0.0;
    long long lastTick = -1;
    std::array<Leg, 3> legs {};
    int legAt = 0, legCount = 0, moveNow = none, cued = none, started = none;
    float stretch = 1.0f, throw16 = 0.0f, freeze16 = 0.0f, freezeIn16 = -1.0f;
    bool throwHeld = false, freezeHeld = false, frozenByHand = false;
    float throwGlow = 0.0f, freezeTarget = 0.0f;
    Touch touch;
};
} // namespace dendrite
