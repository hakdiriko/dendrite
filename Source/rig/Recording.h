// The rig's recorders (3.0): what the window shows, kept on the message thread whether or not
// the window is open. Every number in here comes from the fly's real spikes.
//
// Each tick takes every neuron's spike count since the last one (the fly counts them on the audio
// thread in atomics; nothing here touches the audio thread) and keeps:
//   trace     each neuron's recent firing, an exponential trace with a 300 ms time constant
//   exposure  every spike since the session began, per neuron; saved with the set, so a
//             reopened project shows where this session's music drove the brain
//   raster    each population's rate at every tick, for the last 30 s
//   scars     at each reflex, the 64 neurons that were firing hardest; burned in, never removed,
//             saved with the set (per neuron: how many scars, and when the last one was made)
//   log       what happened, with the session clock; this session only
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "dsp/FlyData.h"
#include <array>
#include <deque>
#include <vector>

namespace dendrite::rig
{
class Recording
{
public:
    static constexpr int neurons = flydata::neurons;
    static constexpr double traceSeconds = 0.3;

    Recording();

    // message thread: fill counts() with each neuron's spikes since the last tick, then step
    uint16_t* counts() noexcept { return incoming.data(); }
    void step (double nowSeconds);

    // the session: since this instance was made (or since the first tick), and how much history
    // a saved set brought back
    double sessionSeconds() const noexcept { return now - started; }
    double restoredSeconds() const noexcept { return restored; }

    float trace (int n) const noexcept { return traces[(size_t) n]; }
    uint32_t exposure (int n) const noexcept { return exposed[(size_t) n]; }
    uint32_t mostExposed() const noexcept { return exposedMax; }
    juce::int64 spikesInSession() const noexcept { return sessionSpikes; }
    float spikesPerSecond() const noexcept { return rate; } // the whole cut brain, now

    // the raster: populations, each a set of neurons the fly really has
    enum Population { earsLeft, earsRight, eyesLeft, eyesRight, smell, kenyon, mbon, ppl1, pam, steerLeft, steerRight, groom, giantFiber, populations };
    static const char* populationName (int p) noexcept;
    int populationSize (int p) const noexcept { return (int) members[(size_t) p].size(); }
    static constexpr int rasterTicks = 1024; // about 34 s at 30 ticks a second
    int rasterLength() const noexcept { return rasterCount; }
    // ticksAgo 0 = the newest: when it was (session seconds) and each population's rate (Hz per neuron)
    double rasterTime (int ticksAgo) const noexcept { return rasterAt[(size_t) rasterIndex (ticksAgo)]; }
    float rasterRate (int population, int ticksAgo) const noexcept { return rasterData[(size_t) (rasterIndex (ticksAgo) * populations + population)]; }
    // false while no time passes for the specimen (the itch selector off: its brain does not step)
    void setStepping (bool on) noexcept { stepping = on; }

    // scars
    static constexpr int scarNeurons = 64;
    void scar (); // now: burn in the neurons firing hardest
    uint16_t scars (int n) const noexcept { return scarCount[(size_t) n]; }
    float scarAge (int n) const noexcept { return (float) (lived() - scarLast[(size_t) n]); } // seconds since its last scar
    int scarTotal() const noexcept { return scarsMade; }
    double lived() const noexcept { return savedLived + sessionSeconds(); } // the set's whole history, in seconds

    // the log
    struct Event
    {
        uint32_t id = 0;
        double at = 0.0;   // session seconds
        juce::String text;
        uint32_t mark = 0; // keep's mark it can be pulled from (0: none)
    };
    static constexpr int logLength = 256;
    uint32_t log (const juce::String& text, uint32_t mark = 0);
    Event* event (uint32_t id) noexcept;
    const std::deque<Event>& events() const noexcept { return eventList; }

    // the specimen's state (arousal, fatigue, fear, memory as the fly publishes them), sampled
    // once a second for the last ten minutes; not saved
    enum Quantity { arousal, fatigue, fear, memory, quantities };
    static constexpr int historySeconds = 600;
    void noteState (const std::array<float, quantities>& now);
    int historyLength() const noexcept { return historyCount; }
    float history (int quantity, int secondsAgo) const noexcept
    {
        const int at = (historyHead - 1 - secondsAgo + 2 * historySeconds) % historySeconds;
        return historyData[(size_t) (at * quantities + quantity)];
    }

    // the set: saved next to the parameters (any thread; guarded)
    juce::ValueTree save() const;
    void load (const juce::ValueTree&);
    static constexpr const char* tag = "rig";
    static constexpr int version = 1;

private:
    int rasterIndex (int ticksAgo) const noexcept { return (rasterHead - 1 - ticksAgo + 2 * rasterTicks) % rasterTicks; }
    std::array<std::vector<uint16_t>, populations> members;
    std::vector<float> rasterData = std::vector<float> ((size_t) (rasterTicks * populations), 0.0f);
    std::vector<double> rasterAt = std::vector<double> ((size_t) rasterTicks, 0.0);
    int rasterHead = 0, rasterCount = 0;
    bool stepping = true;
    std::vector<uint16_t> scarCount;
    std::vector<double> scarLast;
    std::vector<int> order; // scratch for picking the hardest firing
    int scarsMade = 0;
    std::deque<Event> eventList;
    uint32_t nextEvent = 1;
    std::vector<uint16_t> incoming, lastTick;
    std::vector<float> traces;
    std::vector<uint32_t> exposed;
    uint32_t exposedMax = 0;
    juce::int64 sessionSpikes = 0;
    float rate = 0.0f;
    double started = -1.0, now = 0.0, last = -1.0, restored = 0.0, savedLived = 0.0;
    std::vector<float> historyData = std::vector<float> ((size_t) (historySeconds * quantities), 0.0f);
    int historyHead = 0, historyCount = 0;
    double lastNoted = -1.0;
    mutable juce::SpinLock lock; // exposure crosses to whatever thread the host saves on
};
} // namespace dendrite::rig
