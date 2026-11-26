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
// and for the imaging methods (3.2), each a different memory of the same spikes:
//   calcium   GCaMP6s as a kernel on each neuron's spikes (rises in 0.1 s, decays in 0.8 s)
//   frame     the spikes of the last tick alone (voltage imaging: the indicator forgets in 2.5 ms)
//   photons   each neuron's spikes over the last second (photon counting: one spike, one photon)
//   uptake    2-deoxyglucose: every spike of this session only, never saved, with a time-lapse
//             of how it built up (keyframes that thin out as the session grows, so any length fits);
//             since 3.6 held against what ordinary music costs each neuron in the same time
//             (rig/Reference.h), so the film shows what this session did that other music would not
//   dF/F      (3.6) the calcium against each neuron's own last 20 s, so only change lights up
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "dsp/FlyData.h"
#include <array>
#include <atomic>
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

    // the imaging methods' memories of the spikes
    static constexpr double calciumRise = 0.1, calciumDecay = 0.8, photonSeconds = 1.0;
    float calcium (int n) const noexcept { return caSlow[(size_t) n] - caFast[(size_t) n]; }
    // a steady rate's calcium, so a picture can say what its greys mean in Hz
    static float calciumAt (float hz) noexcept { return hz * (float) (calciumDecay - calciumRise); }
    // dF/F (3.6): the calcium against its own baseline (the last ~20 s), over the indicator's resting
    // glow (as bright as 2 Hz held), so a neuron firing steadily goes dark and only a change lights
    static constexpr double baselineSeconds = 20.0;
    float deltaF (int n) const noexcept { return (calcium (n) - caBase[(size_t) n]) / (calciumAt (2.0f) + caBase[(size_t) n]); }
    uint16_t frameSpikes (int n) const noexcept { return lastTick[(size_t) n]; }
    double frameSeconds() const noexcept { return frameDt; }
    int photons (int n) const noexcept { return (int) photonSum[(size_t) n]; }
    float uptake (int n) const noexcept { return uptakes[(size_t) n]; }
    float mostUptake() const noexcept { return uptakeMax; }
    // 3.6: seconds the specimen has lived this session (its brain stepping), and a neuron's uptake
    // beyond what ordinary music would have cost it in that time (0 when it did no more)
    double steppedSeconds() const noexcept { return stepped; }
    static float referenceHz (int n) noexcept;
    // "beyond" means more than twice what ordinary music costs it (plus half a Hz, so a neuron other
    // music never touches needs real work to count)
    float expectedOf (int n, double seconds) const noexcept { return (referenceHz (n) + 0.5f) * (float) seconds; }
    float uptakeAbove (int n) const noexcept { return aboveOf (uptakes[(size_t) n], n, stepped); }
    float aboveOf (float uptake, int n, double seconds) const noexcept { return std::max (0.0f, uptake - 2.0f * expectedOf (n, seconds)); }
    double lapseStepped (int k) const noexcept { return lapseLived[(size_t) k]; }
    // the 2-DG time-lapse: frame k (0 = the first) of lapseFrames(), when it was taken (session seconds)
    int lapseFrames() const noexcept { return (int) lapseAt.size(); }
    double lapseTime (int k) const noexcept { return lapseAt[(size_t) k]; }
    const float* lapseFrame (int k) const noexcept { return lapse[(size_t) k].data(); }

    // how the specimen is being looked at: the method and the goniometer's two angles (degrees);
    // the window's, kept here so it survives the window closing, and saved with the set
    enum Method { traceMethod, twoPhoton, voltage, photonCounting, tracts, deoxyglucose, methods };
    static const char* methodName (int m) noexcept;
    std::atomic<int> method { traceMethod };
    std::atomic<float> yaw { 0.0f }, pitch { 0.0f };
    // the objective: magnification, and where it looks (the field's centre, as fractions of the box)
    std::atomic<float> zoom { 1.0f }, zoomX { 0.5f }, zoomY { 0.5f };
    static constexpr float minZoom = 0.5f, maxZoom = 16.0f;
    // the light (3.3): where its spot sits, as fractions of the plate (after the turn)
    std::atomic<float> lightX { 0.5f }, lightY { 0.5f };

    // the raster: populations, each a set of neurons the fly really has
    enum Population { earsLeft, earsRight, eyesLeft, eyesRight, smell, kenyon, mbon, ppl1, pam, steerLeft, steerRight, groom, giantFiber, populations };
    static const char* populationName (int p) noexcept;
    int populationSize (int p) const noexcept { return (int) members[(size_t) p].size(); }
    const std::vector<uint16_t>& population (int p) const noexcept { return members[(size_t) p]; }
    static constexpr int rasterTicks = 1024; // about 34 s at 30 ticks a second
    int rasterLength() const noexcept { return rasterCount; }
    // ticksAgo 0 = the newest: when it was (session seconds) and each population's rate (Hz per neuron)
    double rasterTime (int ticksAgo) const noexcept { return rasterAt[(size_t) rasterIndex (ticksAgo)]; }
    float rasterRate (int population, int ticksAgo) const noexcept { return rasterData[(size_t) (rasterIndex (ticksAgo) * populations + population)]; }
    // 3.6: the focus's neurons, watched: their mean rate at every raster tick (from now on)
    void watch (const std::vector<uint16_t>& who) { watched = who; std::fill (watchData.begin(), watchData.end(), -1.0f); }
    bool watching() const noexcept { return ! watched.empty(); }
    float watchRate (int ticksAgo) const noexcept { return watchData[(size_t) rasterIndex (ticksAgo)]; } // -1: not yet watched then
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
    std::vector<uint16_t> watched;
    std::vector<float> watchData = std::vector<float> ((size_t) rasterTicks, -1.0f);
    bool stepping = true;
    std::vector<uint16_t> scarCount;
    std::vector<double> scarLast;
    std::vector<int> order; // scratch for picking the hardest firing
    int scarsMade = 0;
    std::deque<Event> eventList;
    uint32_t nextEvent = 1;
    std::vector<uint16_t> incoming, lastTick;
    std::vector<float> traces, caFast, caSlow, caBase, uptakes;
    std::vector<uint16_t> photonRing, photonSum;
    std::array<double, 128> photonAt {};
    int photonHead = 0, photonTail = 0;
    float uptakeMax = 0.0f;
    std::vector<std::vector<float>> lapse;
    std::vector<double> lapseAt, lapseLived;
    double stepped = 0.0;
    double lapseEvery = 1.0, frameDt = 0.0;
    static constexpr int lapseLength = 120;
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
