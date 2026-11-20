// The rig's recorders (3.0): what the window shows, kept on the message thread whether or not
// the window is open. Every number in here comes from the fly's real spikes.
//
// Each tick takes every neuron's spike count since the last one (the fly counts them on the audio
// thread in atomics; nothing here touches the audio thread) and keeps:
//   trace     each neuron's recent firing, an exponential trace with a 300 ms time constant
//   exposure  every spike since the session began, per neuron; saved with the set, so a
//             reopened project shows where this session's music drove the brain
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "dsp/FlyData.h"
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

    // the set: saved next to the parameters (any thread; guarded)
    juce::ValueTree save() const;
    void load (const juce::ValueTree&);
    static constexpr const char* tag = "rig";
    static constexpr int version = 1;

private:
    std::vector<uint16_t> incoming;
    std::vector<float> traces;
    std::vector<uint32_t> exposed;
    uint32_t exposedMax = 0;
    juce::int64 sessionSpikes = 0;
    float rate = 0.0f;
    double started = -1.0, now = 0.0, last = -1.0, restored = 0.0, savedLived = 0.0;
    mutable juce::SpinLock lock; // exposure crosses to whatever thread the host saves on
};
} // namespace dendrite::rig
