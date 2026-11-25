#include "Processor.h"
#include "Editor.h"

const juce::StringArray DendriteProcessor::itchNames { "off",       "1/4 bar",   "1/2 bar", "3/4 bar", "1 bar",  "1.25 bars",
                                                       "1.5 bars",  "1.75 bars", "2 bars",  "3 bars",  "4 bars", "8 bars" };

// the atlas (3.3): each region's own numbers, centred on the fly's defaults (Fly::Taste)
const std::array<DendriteProcessor::TasteParam, 8> DendriteProcessor::tasteParams { {
    { "learn", "mushroom body learn", "", 0.0f, 0.3f, dendrite::Fly::Taste {}.learn, false },
    { "forget", "mushroom body forget", " s", 60.0f, 3600.0f, dendrite::Fly::Taste {}.forget, true },
    { "jumpy", "giant fiber jumpy", "x", 0.2f, 2.5f, dendrite::Fly::Taste {}.jumpy, true },
    { "lurch", "giant fiber lurch", " oct", 0.0f, 1.0f, dendrite::Fly::Taste {}.lurch, false },
    { "scratch", "grooming scratch", "", 0.0f, 2.0f, dendrite::Fly::Taste {}.scratch, false },
    { "lean", "steering lean", "", 0.0f, 1.0f, dendrite::Fly::Taste {}.lean, false },
    { "eyesTire", "eyes tire", " /s", 0.5f, 16.0f, dendrite::Fly::Taste {}.eyesTire, true },
    { "eyesBack", "eyes back", " s", 1.0f, 30.0f, dendrite::Fly::Taste {}.eyesBack, true },
} };

namespace
{
    // IDs and the state type are what hosts store; they never change once shipped.
    // "viber" is the state type every dendrite build has used since 0.0.
    constexpr int paramVersion = 8;

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "feed", paramVersion }, "feed", NormalisableRange<float> (0.0f, 1.0f), DendriteProcessor::defaultFeed,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                return DendriteProcessor::feedName (v) + " " + String (roundToInt (v * 100.0f)) + "%"; })));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "space", paramVersion }, "space", NormalisableRange<float> (0.0f, 1.0f), DendriteProcessor::defaultSpace,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                return DendriteProcessor::spaceName (v) + " " + String (roundToInt (v * 100.0f)) + "%"; })));
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "throw", paramVersion }, "throw", false,
            AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return v ? String ("throwing") : String ("-"); })));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "frames", paramVersion }, "itch selector",
                                                            DendriteProcessor::itchNames, DendriteProcessor::defaultItch));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", paramVersion }, "bypass", false));
        // 1.2: the echo's rhythm, and dry off for return tracks
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "time", 9 }, "time",
                                                            StringArray { "dotted", "triplet", "half" }, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "dry", 9 }, "dry", true));
        // 3.3: the atlas, the fly's own numbers by region; the middle of each is where the fly is born
        for (const auto& t : DendriteProcessor::tasteParams)
        {
            NormalisableRange<float> range (t.lo, t.hi);
            range.setSkewForCentre (t.Fly_Taste_default);
            const String unit (t.unit);
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { t.id, 10 }, t.name, range, t.Fly_Taste_default,
                AudioParameterFloatAttributes().withStringFromValueFunction ([unit] (float v, int) {
                    return String (v, v < 1.0f ? 3 : v < 100.0f ? 2 : 0) + unit; })));
        }
        return layout;
    }
} // namespace

juce::String DendriteProcessor::feedName (float v)
{
    if (v < 0.02f) return "off";
    if (v < 0.3f) return "whisper";
    if (v < 0.62f) return "dub";
    if (v < 0.87f) return "drown";
    return "endless";
}

juce::String DendriteProcessor::spaceName (float v)
{
    if (v < 0.2f) return "inside";
    if (v < 0.5f) return "around";
    if (v < 0.78f) return "envelop";
    return "beyond";
}

DendriteProcessor::DendriteProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      params (*this, nullptr, "viber", makeLayout())
{
    feedParam = params.getRawParameterValue ("feed");
    spaceParam = params.getRawParameterValue ("space");
    throwParam = params.getRawParameterValue ("throw");
    itchParam = params.getRawParameterValue ("frames");
    bypassParam = params.getRawParameterValue ("bypass");
    timeParam = params.getRawParameterValue ("time");
    dryParam = params.getRawParameterValue ("dry");
    for (size_t k = 0; k < tasteParams.size(); ++k)
        tasteParam[k] = params.getRawParameterValue (tasteParams[k].id);

    // Every dendrite is born a creature of its own: a genome from the system's randomness.
    auto genome = (uint64_t) juce::Random::getSystemRandom().nextInt64();
    genome ^= (uint64_t) juce::Time::getHighResolutionTicks() * 0x9E3779B97F4A7C15ull;
    genome ^= (uint64_t) (juce::pointer_sized_uint) this;
    engine.creature().beBorn (genome);
    vault.life = engine.creature().saved();
    engine.itchFly().save (flyVault->memory);

    // the rig records on the message thread, window open or not (3.0)
    rigTimer.startTimerHz (30);
}

DendriteProcessor::~DendriteProcessor()
{
    rigTimer.stopTimer();
}

void DendriteProcessor::rigTick (double nowSeconds)
{
    constexpr auto relaxed = std::memory_order_relaxed;
    auto& d = display;
    const int itchChoice = juce::roundToInt (itchParam->load());
    auto* counts = recording.counts();
    for (int n = 0; n < dendrite::rig::Recording::neurons; ++n)
        counts[n] = takeFlySpikes (n);
    recording.setStepping (itchChoice > 0); // off: the fly rests, its brain does not step
    recording.step (nowSeconds);
    recording.noteState ({ d.flyArousal.load (relaxed), d.flyFatigue.load (relaxed), d.flyFear.load (relaxed), d.flyMemory.load (relaxed) });
    timeline.setSampleRate (d.sampleRate.load (relaxed));
    {
        const auto scope = d.traceFifo.read (d.traceFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i)
            timeline.add (d.traceFrames[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)
            timeline.add (d.traceFrames[(size_t) (scope.startIndex2 + i)]);
    }
    watchRig();
}

void DendriteProcessor::watchRig()
{
    // what happened since the last tick, written into the rig's log (message thread)
    constexpr auto relaxed = std::memory_order_relaxed;
    auto& d = display;
    auto& w = watch;
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const double now = recording.sessionSeconds();
    auto signedText = [] (float v, int decimals) { return (v >= 0.0f ? "+" : "-") + juce::String (std::abs (v), decimals); };

    // the session, once the host has prepared it; what a reopened set brought with it
    const float sr = d.sampleRate.load (relaxed);
    if (! w.sessionLogged && sr > 0.0f)
    {
        w.sessionLogged = true;
        recording.log ("session" + dot + juce::Time::getCurrentTime().formatted ("%d %b %H:%M").toLowerCase());
        recording.log (juce::String (sr / 1000.0f, 1) + " kHz" + dot + juce::String (d.blockSize.load (relaxed)) + " smp");
    }
    if (w.restoredPending)
    {
        w.restoredPending = false;
        const auto h = (int) (recording.restoredSeconds() / 3600.0), m = (int) std::fmod (recording.restoredSeconds() / 60.0, 60.0);
        recording.log ("restored" + dot + (h > 0 ? juce::String (h) + " h " : juce::String()) + juce::String (m) + " min of history" + dot
                       + juce::String (recording.scarTotal()) + (recording.scarTotal() == 1 ? " scar" : " scars"));
    }

    // the input: gone after 2 s below -80 dBFS, back as soon as it rises
    const bool heard = d.inLevel.load (relaxed) > 1.0e-4f;
    if (heard)
        w.quietSince = -1.0;
    else if (w.quietSince < 0.0)
        w.quietSince = now;
    if (w.inputOn && ! heard && now - w.quietSince >= 2.0)
    {
        w.inputOn = false;
        recording.log ("no input");
    }
    else if (! w.inputOn && heard)
    {
        w.inputOn = true;
        recording.log ("input");
    }

    // a reflex: the giant fiber's escape lurches the tape. Logged with its move and the furthest the
    // tape's speed went; at its first tick the neurons firing hardest are burned in as a scar
    const float jump = d.flyJump.load (relaxed), st = d.flySemitones.load (relaxed);
    static const char* moves[] = { "none", "hop", "leap", "duck", "dive", "rush", "hold", "sink" };
    if (jump > 0.01f && ! w.jumping)
    {
        w.jumping = true;
        w.reflexSince = now;
        w.peakSemitones = st;
        const int move = juce::jlimit (0, 7, d.flyMove.load (relaxed));
        w.reflexMove = moves[move];
        recording.scar();
        w.reflexEvent = recording.log ("reflex" + dot + w.reflexMove + " " + signedText (st, 1) + " st");
    }
    else if (jump > 0.01f)
    {
        if (std::abs (st) > std::abs (w.peakSemitones))
        {
            w.peakSemitones = st;
            if (auto* e = recording.event (w.reflexEvent))
                e->text = "reflex" + dot + w.reflexMove + " " + signedText (st, 1) + " st";
        }
    }
    else
        w.jumping = false;

    // grooming: the band it scratches, and for how long (with the fly's own hysteresis)
    const float scratch = d.flyScratch.load (relaxed);
    if (! w.grooming && scratch > 0.3f)
    {
        w.grooming = true;
        w.groomSince = now;
        w.groomEvent = recording.log ("groom" + dot + juce::String (d.flyScratchHz.load (relaxed) / 1000.0f, 1) + " kHz");
    }
    else if (w.grooming && scratch < 0.08f)
    {
        w.grooming = false;
        if (auto* e = recording.event (w.groomEvent))
            e->text += " " + juce::String (now - w.groomSince, 1) + " s";
    }

    // keep's diary: each itch, throw and runaway it holds can be pulled from its log line
    keep.collect();
    for (const auto& m : keep.diary())
    {
        if (m.id <= w.lastMark)
            continue;
        w.lastMark = m.id;
        uint32_t id = 0;
        if (m.kind == dendrite::keep::Kind::itch)
        {
            // the itch's sound is the reflex the rig just logged: that line becomes its handle
            auto* e = recording.event (w.reflexEvent);
            if (e != nullptr && e->mark == 0 && now - e->at < 2.0)
            {
                e->mark = m.id;
                id = e->id;
            }
            else
                id = recording.log ("itch", m.id);
        }
        else
            id = recording.log (m.kind == dendrite::keep::Kind::throwing ? "throw" : "runaway", m.id);
        if (m.kind != dendrite::keep::Kind::itch)
            w.open.push_back ({ m.id, id });
    }
    for (auto it = w.open.begin(); it != w.open.end();)
    {
        const auto* m = keep.find (it->first);
        if (m == nullptr || m->endSample >= 0)
        {
            if (auto* e = recording.event (it->second); e != nullptr && m != nullptr)
                e->text += dot + juce::String ((double) (m->endSample - m->sample) / keep.sampleRate(), 1) + " s";
            it = w.open.erase (it);
        }
        else
            ++it;
    }

    // the protocol: every switch the hand or the host turns
    const int itch = juce::roundToInt (itchParam->load()), time = juce::roundToInt (timeParam->load());
    const bool dry = dryParam->load() > 0.5f, bypassed = bypassParam->load() > 0.5f;
    if (w.seenSettings)
    {
        if (itch != w.itch)
            recording.log ("itch selector" + dot + itchNames[itch] + (itch == 0 ? dot + "at rest" : juce::String()));
        if (time != w.time)
            recording.log ("heads" + dot + params.getParameter ("time")->getCurrentValueAsText());
        if (dry != w.dry)
            recording.log (dry ? "dry" + dot + "0 dB" : "dry" + dot + "off");
        if (bypassed != w.bypassed)
            recording.log (bypassed ? "output" + dot + "bypassed" + dot + "recording" : "output" + dot + "live");
    }
    w.seenSettings = true;
    w.itch = itch;
    w.time = time;
    w.dry = dry;
    w.bypassed = bypassed;
}

juce::AudioProcessorParameter* DendriteProcessor::getBypassParameter() const { return params.getParameter ("bypass"); }

bool DendriteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The space lives between two ears, so the output is always stereo.
    // The sidechain (3.4) is optional: off, mono or stereo; only its hits are used.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto side = layouts.inputBuses.size() > 1 ? layouts.getChannelSet (true, 1) : juce::AudioChannelSet::disabled();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono())
        && (side.isDisabled() || side == juce::AudioChannelSet::stereo() || side == juce::AudioChannelSet::mono());
}

void DendriteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    setLatencySamples (0);
    display.sampleRate.store ((float) sampleRate, std::memory_order_relaxed);
    display.blockSize.store (samplesPerBlock, std::memory_order_relaxed);
    engine.setControls (feedParam->load(), spaceParam->load(), false, (int) itchParam->load(), bypassParam->load() > 0.5f);
    engine.setStyle ((int) timeParam->load(), dryParam->load() > 0.5f);
    engine.prepare ((float) sampleRate);
    keep.prepare (sampleRate, std::max (samplesPerBlock, 512));
    if (sideLeft.size() < (size_t) keep.tapSize())
    {
        sideLeft.assign ((size_t) keep.tapSize(), 0.0f);
        sideRight.assign ((size_t) keep.tapSize(), 0.0f);
    }
}

void DendriteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, false); }

void DendriteProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, true); }

void DendriteProcessor::run (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;

    // a creature loaded from the set takes over here, between two blocks
    int ready = 2;
    if (arrivingState.compare_exchange_strong (ready, 3, std::memory_order_acquire))
    {
        engine.creature().adopt (arriving);
        arrivingState.store (0, std::memory_order_release);
        keptAge = 0;
    }
    // so does a fly's memory, and a new fly when the window asks for one
    int flyReady = 2;
    if (flyArrivingState.compare_exchange_strong (flyReady, 3, std::memory_order_acquire))
    {
        engine.itchFly().adopt (*flyArriving);
        flyArrivingState.store (0, std::memory_order_release);
        flyUntilSeal = 0;
    }
    if (flyForget.exchange (false, std::memory_order_acq_rel))
    {
        engine.itchFly().forget();
        flyUntilSeal = 0;
    }

    // the light's spot, when the window moved it
    if (litReady.load (std::memory_order_acquire) == 1)
    {
        engine.itchFly().setLit (litPending.data(), litCount);
        litTaken.store (litCount, std::memory_order_relaxed);
        litReady.store (0, std::memory_order_release);
    }
    // the atlas: the fly's own numbers, only when one moved (a set that never moves them is untouched)
    {
        auto& fly = engine.itchFly();
        auto t = fly.getTaste();
        float* field[8] = { &t.learn, &t.forget, &t.jumpy, &t.lurch, &t.scratch, &t.lean, &t.eyesTire, &t.eyesBack };
        bool moved = false;
        for (size_t k = 0; k < tasteParams.size(); ++k)
        {
            const float v = tasteParam[k]->load();
            if (std::abs (v - *field[k]) > 1.0e-4f * (tasteParams[k].hi - tasteParams[k].lo))
            {
                *field[k] = v;
                moved = true;
            }
        }
        if (moved)
            fly.setTaste (t);
    }

    const int n = buffer.getNumSamples();
    const int inChannels = getTotalNumInputChannels();
    for (int c = inChannels; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    if (buffer.getNumChannels() < 2 || n == 0)
        return;
    // the main input (mono or stereo) and the sidechain, where the host put them in the buffer
    const bool monoIn = getMainBusNumInputChannels() < 2;
    const int sideChannels = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() ? getChannelCountOfBus (true, 1) : 0;
    const int sideFirst = sideChannels > 0 ? getChannelIndexInProcessBlockBuffer (true, 1, 0) : -1;
    const int sideSecond = sideChannels > 1 ? getChannelIndexInProcessBlockBuffer (true, 1, 1) : sideFirst;
    const bool sidechain = sideFirst >= 0 && sideSecond < buffer.getNumChannels() && ! sideLeft.empty();
    if (monoIn && ! sidechain)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    // The clock: Live's tempo and position while it plays.
    hostTime.playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostTime.bpm = juce::jlimit (30.0, 300.0, *bpm);
            if (auto sig = pos->getTimeSignature())
                if (sig->numerator > 0 && sig->denominator > 0)
                    hostTime.barBeats = sig->numerator * 4.0 / sig->denominator;
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                {
                    hostTime.ppq = *ppq;
                    hostTime.playing = true;
                }
        }

    engine.setControls (feedParam->load(), spaceParam->load(), throwParam->load() > 0.5f, (int) itchParam->load(),
                        forceBypass || bypassParam->load() > 0.5f);
    engine.setStyle ((int) timeParam->load(), dryParam->load() > 0.5f);
    // in pieces no longer than keep's tap, so the heads can be recorded whatever block the host sends
    {
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        const int frameChoice = (int) itchParam->load();
        engine.setItchTap (keep.itchTapLeft(), keep.itchTapRight());
        auto piece = hostTime;
        for (int done = 0; done < n;)
        {
            const int m = std::min (n - done, keep.tapSize());
            if (sidechain)
            {
                // copy the sidechain out first: with a mono input its first channel is the output's right
                const int s = std::min (m, (int) sideLeft.size());
                std::copy_n (buffer.getReadPointer (sideFirst) + done, s, sideLeft.data());
                std::copy_n (buffer.getReadPointer (sideSecond) + done, s, sideRight.data());
                if (monoIn)
                    std::copy_n (left + done, m, right + done);
                engine.setSidechain (sideLeft.data(), sideRight.data());
            }
            engine.process (left + done, right + done, m, piece);
            keep.capture (left + done, right + done, m, engine.view(), piece, frameChoice);
            pushTrace (m, piece);
            piece.ppq += m * piece.bpm / 60.0 / getSampleRate();
            done += m;
        }
    }

    // Never hand a host a NaN or an infinity.
    for (int c = 0; c < 2; ++c)
    {
        const auto* p = buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (p[i]))
            {
                buffer.clear();
                engine.reset();
                return;
            }
    }
    publish();
    keepLife();
    keepFly (n);
}

void DendriteProcessor::pushTrace (int samples, const dendrite::HostTime& time) noexcept
{
    // one frame of the timeline: no allocation, no lock; when the FIFO is full the frame is dropped
    const auto& v = engine.view();
    auto& d = display;
    const auto scope = d.traceFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
    {
        d.traceDropped.fetch_add (1, std::memory_order_relaxed);
        return;
    }
    auto& f = d.traceFrames[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
    f.samples = samples;
    f.in = v.inPeak;
    f.wet = v.wetPeak;
    f.semitones = v.fly.semitones;
    f.jump = v.fly.jump;
    f.scratch = v.fly.scratch;
    f.scratchHz = v.fly.scratchHz;
    f.turn = v.fly.turn;
    f.bpm = (float) v.bpm;
    f.barBeats = v.barBeats;
    f.beat = v.beatAtStart;
    f.move = v.fly.move;
    f.armed = v.fly.armed;
    f.armedSize = v.fly.armedSize;
    f.armedLand16 = v.fly.armedLand16;
    f.cause = v.fly.cause;
    f.thrown = throwParam->load() > 0.5f;
    f.playing = time.playing;
    f.kick = v.kickHits > 0 ? std::max (v.kick, 0.01f) : 0.0f;
    f.kickStartle = v.kickHits > 0 ? v.kickStartle : 0.0f;
    f.kickOn = v.kickOn;
}

void DendriteProcessor::keepFly (int samples) noexcept
{
    flyUntilSeal -= samples;
    if (flyUntilSeal > 0)
        return;
    flyUntilSeal = (int) (2.0 * getSampleRate());
    auto& v = *flyVault;
    v.seq.fetch_add (1, std::memory_order_acq_rel); // odd: writing
    std::atomic_thread_fence (std::memory_order_release);
    engine.itchFly().save (v.memory);
    v.seq.fetch_add (1, std::memory_order_release); // even: done
}

void DendriteProcessor::readFly (FlyMemory& out) const
{
    if (flyArrivingState.load (std::memory_order_acquire) == 2)
    {
        out = *flyArriving;
        return;
    }
    const auto& v = *flyVault;
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        const auto before = v.seq.load (std::memory_order_acquire);
        if ((before & 1u) != 0)
        {
            std::this_thread::yield();
            continue;
        }
        std::memcpy (&out, &v.memory, sizeof (out));
        std::atomic_thread_fence (std::memory_order_acquire);
        if (v.seq.load (std::memory_order_relaxed) == before)
            return;
    }
}

void DendriteProcessor::keepLife() noexcept
{
    const auto& life = engine.creature().life();
    if (life.age < keptAge + 16 && life.itches == keptItches && keptAge != 0)
        return;
    keptAge = std::max<uint64_t> (1, life.age);
    keptItches = life.itches;
    vault.seq.fetch_add (1, std::memory_order_acq_rel); // odd: writing
    std::atomic_thread_fence (std::memory_order_release);
    vault.life = engine.creature().saved();
    vault.seq.fetch_add (1, std::memory_order_release); // even: done
}

dendrite::Creature::Life DendriteProcessor::readLife() const
{
    if (arrivingState.load (std::memory_order_acquire) == 2)
        return arriving;
    dendrite::Creature::Life copy;
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        const auto before = vault.seq.load (std::memory_order_acquire);
        if ((before & 1u) != 0)
        {
            std::this_thread::yield();
            continue;
        }
        std::memcpy (&copy, &vault.life, sizeof (copy));
        std::atomic_thread_fence (std::memory_order_acquire);
        if (vault.seq.load (std::memory_order_relaxed) == before)
            return copy;
    }
    return copy;
}

void DendriteProcessor::publish()
{
    const auto& v = engine.view();
    auto& d = display;
    constexpr auto relaxed = std::memory_order_relaxed;
    d.dotted.store (v.echo.dottedSeconds, relaxed);
    d.quarter.store (v.echo.quarterSeconds, relaxed);
    d.gainDotted.store (v.echo.gainDotted, relaxed);
    d.gainQuarter.store (v.echo.gainQuarter, relaxed);
    d.pan.store (v.echo.pan, relaxed);
    d.spread.store (v.echo.spread, relaxed);
    d.diffuse.store (v.echo.diffuse, relaxed);
    d.rotate.store (v.echo.rotate, relaxed);
    d.theta.store (v.echo.theta, relaxed);
    d.returnLevel.store (v.echo.returnLevel, relaxed);
    d.fill.store (v.echo.fill, relaxed);
    d.throwing.store (v.echo.throwing, relaxed);
    d.breath.store (v.echo.breath, relaxed);
    d.repetition.store (v.repetition, relaxed);
    d.frameProgress.store (v.frameProgress, relaxed);
    d.boredom.store (v.boredom, relaxed);
    for (size_t h = 0; h < d.lamps.size(); ++h)
        d.lamps[h].store (v.lamps[h], relaxed);
    d.correlation.store (v.correlation, relaxed);
    d.inLevel.store (v.inLevel, relaxed);
    d.outLevel.store (v.outLevel, relaxed);
    // hit is held at its highest until the window takes it (d.hit.exchange (0.0f))
    for (float prev = d.hit.load (relaxed); v.hit > prev && ! d.hit.compare_exchange_weak (prev, v.hit, relaxed);)
        ;
    d.body.store (v.body, relaxed);
    d.echoBody.store (v.echoBody, relaxed);
    d.runaway.store (v.runaway, relaxed);
    d.glow.store (v.echo.glow, relaxed);
    d.bpm.store ((float) v.bpm, relaxed);
    d.hostPlaying.store (v.hostPlaying, relaxed);
    d.itching.store (v.itching, relaxed);
    d.flyJump.store (v.fly.jump, relaxed);
    d.flySemitones.store (v.fly.semitones, relaxed);
    d.flyScratch.store (v.fly.scratch, relaxed);
    d.flyScratchHz.store (v.fly.scratchHz, relaxed);
    d.flyTurn.store (v.fly.turn, relaxed);
    d.flyLean.store (v.fly.lean, relaxed);
    d.flyEyes.store (v.fly.eyes, relaxed);
    d.flyArousal.store (v.fly.arousal, relaxed);
    d.flyFatigue.store (v.fly.fatigue, relaxed);
    d.flyFear.store (v.fly.fear, relaxed);
    d.flyMemory.store (v.fly.memory, relaxed);
    d.flyThrowing.store (v.fly.throwing, relaxed);
    d.flyFrozen.store (v.fly.frozen, relaxed);
    d.flyAwake.store (v.flyAwake, relaxed);
    d.flyMove.store (v.fly.move, relaxed);

    float level = 0.0f;
    bool thrown = false;
    if (engine.takeOnset (level, thrown))
    {
        const auto scope = d.fifo.write (1);
        if (scope.blockSize1 > 0)
            d.onsets[(size_t) scope.startIndex1] = { level, thrown };
    }
}

void DendriteProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.removeChild (state.getChildWithName ("creature"), nullptr);

    // the creature's life, sealed (about 5.5 KB), next to the parameters
    const auto life = readLife();
    juce::ValueTree creature ("creature");
    creature.setProperty ("genome", juce::String::toHexString ((juce::int64) life.genome), nullptr);
    creature.setProperty ("age", (juce::int64) life.age, nullptr);
    creature.setProperty ("itches", (int) life.itches, nullptr);
    creature.setProperty ("life", juce::Base64::toBase64 (&life, sizeof (life)), nullptr);
    state.appendChild (creature, nullptr);

    // the fly's memory and moods (about 77 KB as text), so reopening the set brings back the same fly
    state.removeChild (state.getChildWithName ("fly"), nullptr);
    auto flyScratch = std::make_unique<FlyMemory>();
    readFly (*flyScratch);
    if (flyScratch->valid())
    {
        juce::ValueTree fly ("fly");
        fly.setProperty ("lived", flyScratch->lived, nullptr);
        fly.setProperty ("memory", juce::Base64::toBase64 (flyScratch.get(), sizeof (FlyMemory)), nullptr);
        state.appendChild (fly, nullptr);
    }

    // the rig's record of this session (3.0): where the music drove the brain
    state.removeChild (state.getChildWithName (dendrite::rig::Recording::tag), nullptr);
    state.appendChild (recording.save(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void DendriteProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);

    // A set saved with a creature brings it back; a set from before 2.0 (or a broken life)
    // keeps the newborn this instance already is.
    const auto creature = state.getChildWithName ("creature");
    if (creature.isValid())
    {
        juce::MemoryOutputStream bytes;
        if (juce::Base64::convertFromBase64 (bytes, creature.getProperty ("life").toString())
            && bytes.getDataSize() == sizeof (dendrite::Creature::Life))
        {
            dendrite::Creature::Life life;
            std::memcpy (&life, bytes.getData(), sizeof (life));
            if (life.valid())
            {
                for (int spins = 0;; ++spins)
                {
                    int expected = arrivingState.load (std::memory_order_acquire);
                    if ((expected == 0 || expected == 2)
                        && arrivingState.compare_exchange_weak (expected, 1, std::memory_order_acquire))
                        break;
                    if (spins > 100000)
                        return; // the audio thread is stuck adopting: keep what we have
                    std::this_thread::yield();
                }
                arriving = life;
                arrivingState.store (2, std::memory_order_release);
            }
        }
        state.removeChild (creature, nullptr);
    }

    // A set saved with a fly brings back what it learned; a set from before 2.3 keeps this fly.
    const auto fly = state.getChildWithName ("fly");
    if (fly.isValid())
    {
        juce::MemoryOutputStream bytes;
        if (juce::Base64::convertFromBase64 (bytes, fly.getProperty ("memory").toString())
            && bytes.getDataSize() == sizeof (FlyMemory))
        {
            auto flyScratch = std::make_unique<FlyMemory>();
            std::memcpy (flyScratch.get(), bytes.getData(), sizeof (FlyMemory));
            if (flyScratch->valid())
            {
                bool claimed = false;
                for (int spins = 0; spins <= 100000; ++spins)
                {
                    int expected = flyArrivingState.load (std::memory_order_acquire);
                    if ((expected == 0 || expected == 2)
                        && flyArrivingState.compare_exchange_weak (expected, 1, std::memory_order_acquire))
                    {
                        claimed = true;
                        break;
                    }
                    std::this_thread::yield();
                }
                if (claimed)
                {
                    *flyArriving = *flyScratch;
                    flyArrivingState.store (2, std::memory_order_release);
                }
            }
        }
        state.removeChild (fly, nullptr);
    }

    // the rig's history (3.0); sets from before 3.0 have none and start fresh
    if (const auto rig = state.getChildWithName (dendrite::rig::Recording::tag); rig.isValid())
    {
        recording.load (rig);
        state.removeChild (rig, nullptr);
        watch.restoredPending = true;
    }
    params.replaceState (state);
}

juce::AudioProcessorEditor* DendriteProcessor::createEditor() { return new DendriteEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DendriteProcessor(); }
