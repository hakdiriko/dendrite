#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "keep/Keep.h"
#include "rig/Recording.h"
#include "rig/Timeline.h"

class DendriteProcessor : public juce::AudioProcessor
{
public:
    DendriteProcessor();
    ~DendriteProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    static constexpr float defaultFeed = 0.5f, defaultSpace = 0.4f;
    static constexpr int defaultItch = dendrite::Engine::defaultFrame; // 4 bars
    static const juce::StringArray itchNames;

    static juce::String feedName (float feed);
    static juce::String spaceName (float space);

    // What the window shows. Written by the audio thread once per block, read by the UI.
    struct Display
    {
        std::atomic<float> dotted { 0.375f }, quarter { 0.5f }, gainDotted { 0.62f }, gainQuarter { 0.42f };
        std::atomic<float> pan { 0.0f }, spread { 0.0f }, diffuse { 0.0f }, rotate { 0.0f }, theta { 0.0f };
        std::atomic<float> returnLevel { 0.0f }, fill { 0.0f }, throwing { 0.0f }, breath { 0.0f };
        std::atomic<float> repetition { 0.0f }, frameProgress { 0.0f }, boredom { 0.0f };
        std::array<std::atomic<float>, dendrite::Heads::count> lamps {};
        std::atomic<float> correlation { 1.0f }, inLevel { 0.0f }, outLevel { 0.0f }, bpm { 120.0f };
        std::atomic<float> sampleRate { 0.0f }; // as the host prepared it (0: not yet)
        std::atomic<int> blockSize { 0 };
        // 0..1 against the track's own recent peaks, so a quiet track looks as alive as a loud
        // one: the strongest hit since the window last took it (read it with exchange (0.0f)),
        // the input's held level, the echo's level
        std::atomic<float> hit { 0.0f }, body { 0.0f }, echoBody { 0.0f };
        std::atomic<float> runaway { 0.0f }; // 0..1 while a throw held at endless runs away
        std::atomic<float> glow { 0.0f };    // 0..1: how much of the itch's afterglow is still alive
        std::atomic<bool> hostPlaying { false }, itching { false };
        // the fly: what it is doing now
        std::atomic<float> flyJump { 0.0f }, flySemitones { 0.0f }, flyScratch { 0.0f }, flyScratchHz { 1000.0f }, flyTurn { 0.0f };
        std::atomic<float> flyArousal { 0.0f }, flyFatigue { 0.0f }, flyFear { 0.0f }, flyMemory { 1.0f };
        std::atomic<bool> flyThrowing { false }, flyFrozen { false }, flyAwake { false };
        std::atomic<int> flyMove { 0 }; // dendrite::Fly::Move

        // onsets: each one starts a new tree in the window
        struct Onset { float level = 0.0f; bool thrown = false; };
        juce::AbstractFifo fifo { 64 };
        std::array<Onset, 64> onsets {};

        // the timeline (3.3): one frame per engine call, audio thread to message thread, lock-free.
        // A full FIFO drops frames (the window was away for over 20 s); they are counted.
        static constexpr int traceCapacity = 4096;
        juce::AbstractFifo traceFifo { traceCapacity };
        std::array<dendrite::rig::Timeline::Frame, traceCapacity> traceFrames {};
        std::atomic<int> traceDropped { 0 };
    } display;

    // The fly's neurons, for the window: how often each fired since the window last asked.
    uint16_t takeFlySpikes (int neuron) noexcept { return engine.itchFly().takeSpikes (neuron); }
    // A new fly: it forgets what it learned and its moods (from the window; done at the next block).
    void forgetFly() noexcept { flyForget.store (true, std::memory_order_release); }
    // The light (3.3): which neurons the spot covers (from the window). Taken at the next block;
    // false while the last one has not been taken yet (the window tries again next frame).
    bool postLit (const uint16_t* lit, int count) noexcept
    {
        if (litReady.load (std::memory_order_acquire) != 0)
            return false;
        litCount = std::clamp (count, 0, (int) litPending.size());
        std::copy (lit, lit + litCount, litPending.begin());
        litReady.store (1, std::memory_order_release);
        return true;
    }
    int litInFly() const noexcept { return litTaken.load (std::memory_order_relaxed); }

    // The fly's own numbers (3.3, the atlas): each region's, as automatable parameters. The defaults
    // are the fly's own, so a set that never moves them sounds as before.
    struct TasteParam { const char* id; const char* name; const char* unit; float lo, hi, Fly_Taste_default; bool log; };
    static const std::array<TasteParam, 8> tasteParams;

    // keep: the last minute, on tape, and a diary of what happened (read by the window)
    dendrite::keep::Keep keep;

    // The rig's recorders (3.0), message thread only: ticked 30 times a second whether or not the
    // window is open (the snapshot tool, which has no message loop, calls rigTick itself).
    dendrite::rig::Recording recording;
    // what the fly did to your sound (3.3), fed from the display's trace FIFO by rigTick
    dendrite::rig::Timeline timeline;
    void rigTick (double nowSeconds);

private:
    void run (juce::AudioBuffer<float>&, bool forceBypass);
    void publish();

    std::atomic<float>* feedParam = nullptr;
    std::atomic<float>* spaceParam = nullptr;
    std::atomic<float>* throwParam = nullptr;
    std::atomic<float>* itchParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::array<std::atomic<float>*, 8> tasteParam {};
    std::array<uint16_t, dendrite::Fly::neurons> litPending {};
    int litCount = 0;
    std::atomic<int> litReady { 0 }, litTaken { 0 };

    dendrite::Engine engine;
    dendrite::HostTime hostTime;
    // the sidechain's piece, copied out before the output is written over it (a mono input puts the
    // sidechain's first channel where the output's right is); sized in prepareToPlay as keep's tap
    std::vector<float> sideLeft, sideRight;

    // The creature's life crosses threads here. The audio thread seals a copy into the vault
    // once per bar and after every itch (a seqlock: a reader that raced a write tries again);
    // a life loaded from a set waits in `arriving` until the next block adopts it.
    void keepLife() noexcept;                 // audio thread
    dendrite::Creature::Life readLife() const; // message thread
    struct Vault
    {
        std::atomic<uint32_t> seq { 0 };
        dendrite::Creature::Life life;
    };
    mutable Vault vault;
    dendrite::Creature::Life arriving;
    std::atomic<int> arrivingState { 0 }; // 0 nothing, 1 being written, 2 ready, 3 being adopted
    uint64_t keptAge = 0;
    uint32_t keptItches = 0;

    // The fly's memory crosses threads the same way: sealed by the audio thread about every two
    // seconds (and at once after it adopts or forgets), read by the message thread to save it.
    using FlyMemory = dendrite::Fly::Memory;
    void keepFly (int samples) noexcept;  // audio thread
    void pushTrace (int samples, const dendrite::HostTime& time) noexcept; // audio thread: one timeline frame
    void readFly (FlyMemory&) const;      // message thread
    struct FlyVault
    {
        std::atomic<uint32_t> seq { 0 };
        FlyMemory memory;
    };
    std::unique_ptr<FlyVault> flyVault = std::make_unique<FlyVault>();
    std::unique_ptr<FlyMemory> flyArriving = std::make_unique<FlyMemory>();
    std::atomic<int> flyArrivingState { 0 }; // 0 nothing, 1 being written, 2 ready, 3 being adopted
    std::atomic<bool> flyForget { false };
    int flyUntilSeal = 0;

    struct RigTimer : juce::Timer
    {
        explicit RigTimer (DendriteProcessor& p) : owner (p) {}
        void timerCallback() override { owner.rigTick (juce::Time::getMillisecondCounterHiRes() * 0.001); }
        DendriteProcessor& owner;
    };
    RigTimer rigTimer { *this };

    // what the rig's log has already seen (message thread)
    void watchRig();
    struct Watch
    {
        bool sessionLogged = false, restoredPending = false, inputOn = true, jumping = false, grooming = false, seenSettings = false;
        double quietSince = -1.0, reflexSince = 0.0, groomSince = 0.0;
        float peakSemitones = 0.0f;
        juce::String reflexMove;
        uint32_t reflexEvent = 0, groomEvent = 0, lastMark = 0;
        std::vector<std::pair<uint32_t, uint32_t>> open; // keep's marks still playing, and their log lines
        int itch = -1, time = -1;
        bool dry = true, bypassed = false;
    } watch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteProcessor)
};
