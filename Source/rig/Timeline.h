// The timeline (3.3): what the fly did to your sound, kept for the window. The audio thread hands
// the processor one Frame per engine call through a lock-free FIFO; on the message thread the
// processor feeds them here. Time is counted in the samples the plug-in really processed, so the
// picture is as long as the audio was. Kept: the last minute in 10 ms bins (the loudest input
// and echo sample of each, the tape's speed, how far the fly's jump and groom were in), and the
// fly's reflexes, grooms and your throws as events with their real start and length.
// Message thread only; nothing here is saved with the set.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dendrite::rig
{
class Timeline
{
public:
    // one engine call, as the audio thread saw it
    struct Frame
    {
        int samples = 0;
        float in = 0.0f, wet = 0.0f;              // loudest input and echo sample (linear)
        float semitones = 0.0f, jump = 0.0f;      // the tape's speed, the fly's jump (0..1)
        float scratch = 0.0f, scratchHz = 0.0f;   // the groom band, how far in, and where
        float turn = 0.0f;                        // steer: -1 right .. 1 left
        float bpm = 120.0f, barBeats = 4.0f;
        double beat = 0.0;                        // the clock at the frame's start (quarter notes)
        int move = 0;                             // dendrite::Fly::Move
        bool thrown = false, playing = false;     // your throw held; the host's transport
        // 3.4: a move decided and waiting for its line (16th it lands on, from beat 0), and
        // which eye's looming fired the giant fiber (0 unknown, 1 left, 2 right, 3 its ears)
        bool armed = false;
        float armedSize = 0.0f;
        double armedLand16 = 0.0;
        int cause = 0;
        // the sidechain (3.4): the strongest kick hit in this frame (0: none; 0..1 of its recent
        // loudest), how much it startled the fly (0..1), and whether a sidechain came at all
        float kick = 0.0f, kickStartle = 0.0f;
        bool kickOn = false;
    };

    struct Bin
    {
        float in = 0.0f, wet = 0.0f, semitones = 0.0f, jump = 0.0f, scratch = 0.0f, turn = 0.0f;
        float kick = 0.0f, kickStartle = 0.0f; // a kick hit in this bin (only in the first bin of its frame)
        double beat = 0.0;
        bool thrown = false, playing = false, kickOn = false;
    };

    // a reflex: when it began (seconds of processed audio), its move, the tape speeds it went
    // through in order, and how long the tape was bent
    struct Reflex
    {
        double at = 0.0, length = 0.0, beat = 0.0;
        int move = 0, steps = 0, cause = 0;
        std::array<float, 4> semitones {};
        bool open = false, playing = false;
    };
    // a groom (the fly's band on the echo) or your throw
    struct Span
    {
        double at = 0.0, length = 0.0;
        float hz = 0.0f;
        bool open = false;
    };

    static constexpr double binSeconds = 0.01;
    static constexpr int binCount = 6000; // a minute
    static constexpr int eventCount = 64;

    void setSampleRate (double rate) noexcept;
    void add (const Frame&) noexcept;

    double now() const noexcept { return sampleRate > 0.0 ? (double) samplesSeen / sampleRate : 0.0; }
    int bins() const noexcept { return filled; }
    const Bin& bin (int back) const noexcept; // 0: the newest finished bin
    double binTime (int back) const noexcept; // when that bin began, in seconds of processed audio
    float bpm() const noexcept { return lastBpm; }
    float barBeats() const noexcept { return lastBarBeats; }
    bool playing() const noexcept { return lastPlaying; }

    int reflexes() const noexcept { return reflexCount; }
    const Reflex& reflex (int back) const noexcept { return reflexRing[(std::size_t) ((reflexHead - 1 - back + eventCount) % eventCount)]; }
    int grooms() const noexcept { return groomCount; }
    const Span& groom (int back) const noexcept { return groomRing[(std::size_t) ((groomHead - 1 - back + eventCount) % eventCount)]; }
    int throws() const noexcept { return throwCount; }
    const Span& throwAt (int back) const noexcept { return throwRing[(std::size_t) ((throwHead - 1 - back + eventCount) % eventCount)]; }

    static const char* moveName (int move) noexcept;

    // the move the fly has decided and the grid has not played yet (3.4)
    bool armed() const noexcept { return lastArmed; }
    float armedSize() const noexcept { return lastArmedSize; }
    double armedLandBeat() const noexcept { return lastArmedLand16 / 4.0; }
    double beat() const noexcept { return lastBeat; } // the clock at the newest frame's end
    int reflexesEver() const noexcept { return reflexesSeen; }

private:
    void finishBin() noexcept;
    Reflex& newReflex() noexcept;
    Span& newSpan (std::array<Span, eventCount>& ring, int& head, int& count) noexcept;

    double sampleRate = 0.0;
    int64_t samplesSeen = 0;
    int binSamples = 480, inBin = 0;
    Bin current;
    bool currentEmpty = true;
    std::array<Bin, binCount> ring {};
    int head = 0, filled = 0;
    int64_t finishedBins = 0;
    float lastBpm = 120.0f, lastBarBeats = 4.0f;
    bool lastPlaying = false;
    bool lastArmed = false;
    float lastArmedSize = 0.0f;
    double lastArmedLand16 = 0.0, lastBeat = 0.0;
    int reflexesSeen = 0;

    std::array<Reflex, eventCount> reflexRing {};
    int reflexHead = 0, reflexCount = 0;
    std::array<Span, eventCount> groomRing {}, throwRing {};
    int groomHead = 0, groomCount = 0, throwHead = 0, throwCount = 0;
    bool jumping = false, grooming = false, throwing = false;
};
} // namespace dendrite::rig
