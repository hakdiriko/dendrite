// The kick (3.4): dendrite's sidechain. Only the hits of what you send it count, never its sound;
// it is never heard and never enters the echo. An onset detector (1 ms attack) finds each hit, and a
// grid of expectations, one per 16th of the bar, remembers how often hits have come there. A hit
// where hits always come barely startles; a hit where none came (a fill, a ghost note) or after the
// kick was gone (the drop) startles hard. The startle goes to the fly's looming eye (LPLC2, the giant
// fiber's input), and the hit ducks the echo: as deep as the fly is afraid, as quick to come back as
// it is wound up. Without a sidechain none of this runs; a silent one never fires.
#pragma once

#include "Tools.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace dendrite
{
class Kick
{
public:
    void prepare (float sampleRate) noexcept
    {
        sr = sampleRate;
        hp.highpass (30.0f, 0.707f, sr);
        lp.lowpass (5000.0f, 0.707f, sr);
        clickHp.highpass (800.0f, 0.707f, sr);
        fastUp = coef (0.001f);
        fastDown = coef (0.012f);
        slowUp = coef (0.06f);
        slowDown = coef (0.3f);
        refDown = coef (10.0f);
        loomDown = coef (0.035f);
        duckUp = 1.0f - coef (0.001f);
        measureFor = (int) std::lround (0.010f * sr);
        holdFor = (int) std::lround (0.015f * sr);
        rearmFor = (int) std::lround (0.040f * sr);
        reset();
    }

    void reset() noexcept
    {
        hp.clear();
        lp.clear();
        clickHp.clear();
        clickFast = clickSlow = 0.0f;
        fast = slow = ref = loomNow = duckEnv = duckTarget = 0.0f;
        hitMax = 0.0f;
        measuring = holding = 0;
        sinceHit = 1 << 30;
        armed = true;
        expect.fill (0.0f);
        lastIndex = INT64_MIN;
        hitIndex = INT64_MIN;
        hitSlot = 0;
        hits = 0;
        lastStrength = lastStartle = 0.0f;
    }

    // per block, from the fly: the duck is deeper as it is afraid, and back sooner as it is wound up
    void setMood (float fear, float arousal) noexcept
    {
        fearNow = clamp01 (fear);
        duckDown = coef (0.08f + 0.42f * (1.0f - clamp01 (arousal)));
    }

    // per sample: the sidechain (mono), the clock in quarter notes, 16ths per bar
    void hear (float x, double beat, int stepsPerBar) noexcept
    {
        const int n = std::clamp (stepsPerBar, 1, maxSlots);
        // the grid: when a 16th ends without its hit, that slot expects less
        const auto index = (int64_t) std::floor (beat * 4.0 + 0.5);
        if (index != lastIndex)
        {
            if (lastIndex != INT64_MIN && hitIndex != lastIndex)
                expect[slotOf (lastIndex, n)] *= missKeep;
            for (int s = 0; s < n; ++s)
                expect[(size_t) s] *= forgetKeep;
            lastIndex = index;
        }

        const float y = std::abs (lp.process (hp.process (x)));
        fast = y + (y > fast ? fastUp : fastDown) * (fast - y);
        slow = y + (y > slow ? slowUp : slowDown) * (slow - y);
        // the beater's click, so a ghost note under the last kick's boom is still a hit
        const float c = std::abs (clickHp.process (x));
        clickFast = c + (c > clickFast ? fastUp : fastDown) * (clickFast - c);
        clickSlow = c + (c > clickSlow ? slowUp : slowDown) * (clickSlow - c);
        ref *= refDown;
        ++sinceHit;
        if (! armed && sinceHit > rearmFor && fast < 1.3f * slow)
            armed = true;
        const bool boom = fast > floorLevel && fast > 2.0f * slow + 1.0e-4f;
        const bool click = clickFast > 0.3f * floorLevel && clickFast > 3.0f * clickSlow + 1.0e-4f;
        if (armed && (boom || click))
        {
            // a hit: duck now, measure its peak over 10 ms, then startle the fly
            armed = false;
            sinceHit = 0;
            measuring = measureFor;
            hitMax = 0.0f;
            hitIndex = index;
            hitSlot = slotOf (index, n);
            // how deep it ducks: the fly's fear, and how unexpected this hit is
            const float surprise = 1.0f - expect[hitSlot];
            depthDb = std::min (12.0f, 2.0f + 6.0f * fearNow + 6.0f * (0.1f + 0.9f * surprise));
        }
        if (measuring > 0)
        {
            hitMax = std::max (hitMax, fast);
            ref = std::max (ref, hitMax);
            const float strength = hitMax / std::max (ref, 1.0e-6f);
            duckTarget = strength * depthDb;
            if (--measuring == 0)
            {
                float& e = expect[(size_t) hitSlot];
                const float surprise = 1.0f - e;
                e += learn * strength * (1.0f - e);
                const float startle = std::sqrt (strength) * (0.1f + 0.9f * surprise); // a quiet hit that is new still startles
                loomNow = std::max (loomNow, 1.2f * startle);
                ++hits;
                lastStrength = strength;
                lastStartle = startle;
                holding = holdFor;
            }
        }
        else
            loomNow *= loomDown;

        // the duck, in dB: down in 1 ms to the hit's depth, held 15 ms, then back (a weaker hit
        // inside a deeper duck lets that one go on coming back)
        if ((measuring > 0 || holding > 0) && duckTarget > duckEnv)
            duckEnv += duckUp * (duckTarget - duckEnv);
        else
            duckEnv *= duckDown;
        if (measuring == 0 && holding > 0)
            --holding;
    }

    // what reaches the fly's looming eye now (0..1.2, as its eyes hear the music)
    float loom() const noexcept { return loomNow; }
    // the echo's gain under the hit (1 when no hit has come)
    float duck() const noexcept { return duckEnv > 1.0e-5f ? std::exp (-duckEnv * (2.302585f / 20.0f)) : 1.0f; }

    // for the window: hits so far, and the last one's strength (0..1 of the recent loudest) and startle
    int hitCount() const noexcept { return hits; }
    float strength() const noexcept { return lastStrength; }
    float startle() const noexcept { return lastStartle; }

private:
    static constexpr int maxSlots = 64;
    static constexpr float floorLevel = 1.0e-3f;    // -60 dBFS
    static constexpr float learn = 0.4f;            // how much one hit teaches its slot
    static constexpr float missKeep = 0.6f;         // a slot that passes without its hit
    static constexpr float forgetKeep = 0.9961f;    // every 16th, every slot: forgotten over about 16 bars
    float coef (float seconds) const noexcept { return std::exp (-1.0f / (seconds * sr)); }
    static size_t slotOf (int64_t index, int n) noexcept { return (size_t) (((index % n) + n) % n); }

    float sr = 48000.0f;
    Biquad hp, lp, clickHp;
    float clickFast = 0.0f, clickSlow = 0.0f, fearNow = 0.0f;
    float fast = 0.0f, slow = 0.0f, ref = 0.0f, hitMax = 0.0f;
    float fastUp = 0, fastDown = 0, slowUp = 0, slowDown = 0, refDown = 0, loomDown = 0;
    float duckUp = 0, duckDown = 0, depthDb = 3.0f;
    float loomNow = 0.0f, duckEnv = 0.0f, duckTarget = 0.0f; // the duck in dB
    int measureFor = 480, holdFor = 720, rearmFor = 1920, measuring = 0, holding = 0, sinceHit = 1 << 30;
    bool armed = true;
    std::array<float, maxSlots> expect {};
    int64_t lastIndex = INT64_MIN, hitIndex = INT64_MIN;
    size_t hitSlot = 0;
    int hits = 0;
    float lastStrength = 0.0f, lastStartle = 0.0f;
};
} // namespace dendrite
