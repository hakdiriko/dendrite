// The null test: runs material through dendrite's processor exactly as a host would (no window),
// and writes what came out as raw 32-bit floats, so two builds can be compared sample for sample.
// Built from any commit since 1.2 (it uses only the processor and its parameter IDs).
//   dendrite_nulltest in.wav out.f32 sampleRate block [feed space itch time dry] [throwAt throwLen] [itchAt itch2]
// The sample rate is the one the processor is prepared at; the file's samples are fed as they are.
// Optional environment:
//   DENDRITE_STATE_IN=file    load this saved state (getStateInformation's bytes) before playing
//   DENDRITE_STATE_OUT=file   save the state after playing
//   DENDRITE_ROUNDTRIP=1      (3.0 on) tick the rig while playing, then check that a new instance loads
//                             its scars and exposure back exactly
#include <juce_audio_formats/juce_audio_formats.h>
#include "Processor.h"
#if __has_include("rig/Recording.h")
 #define HAS_RIG 1
#endif

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 5)
    {
        std::printf ("usage: dendrite_nulltest in.wav out.f32 sampleRate block [feed space itch time dry] [throwAt throwLen] [itchAt itch2]\n");
        return 1;
    }
    auto arg = [&] (int k, double fallback) { return argc > k ? std::atof (argv[k]) : fallback; };
    const double sr = arg (3, 48000.0);
    const int block = (int) arg (4, 512.0);
    const float feed = (float) arg (5, 0.5), space = (float) arg (6, 0.4);
    const int itch = (int) arg (7, 10), time = (int) arg (8, 0), dry = (int) arg (9, 1);
    const double throwAt = arg (10, -1.0), throwLen = arg (11, 0.6), itchAt = arg (12, -1.0);
    const int itch2 = (int) arg (13, 0);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (juce::String (argv[1]))));
    if (reader == nullptr)
        return 2;
    juce::AudioBuffer<float> in (2, (int) reader->lengthInSamples);
    reader->read (&in, 0, in.getNumSamples(), 0, true, true);

    DendriteProcessor processor;
    processor.setPlayConfigDetails (2, 2, sr, block);
    auto set = [&] (const char* id, float plain) {
        auto* p = processor.params.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    set ("feed", feed);
    set ("space", space);
    set ("frames", (float) itch);
    set ("time", (float) time);
    set ("dry", (float) dry);
    if (const char* in = std::getenv ("DENDRITE_STATE_IN"))
    {
        juce::MemoryBlock bytes;
        if (! juce::File (juce::String (in)).loadFileAsData (bytes))
            return 4;
        processor.setStateInformation (bytes.getData(), (int) bytes.getSize());
        std::printf ("state loaded: feed %.3f space %.3f itch %d\n", processor.params.getRawParameterValue ("feed")->load(),
                     processor.params.getRawParameterValue ("space")->load(), (int) processor.params.getRawParameterValue ("frames")->load());
    }
    processor.prepareToPlay (sr, block);
#if HAS_RIG
    const bool roundTrip = std::getenv ("DENDRITE_ROUNDTRIP") != nullptr;
#endif

    juce::FileOutputStream out { juce::File (juce::String (argv[2])) };
    out.setPosition (0);
    out.truncate();
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (int at = 0; at < in.getNumSamples(); at += block)
    {
        const int n = std::min (block, in.getNumSamples() - at);
        const double t = at / sr;
        set ("throw", throwAt >= 0.0 && t >= throwAt && t < throwAt + throwLen ? 1.0f : 0.0f);
        if (itchAt >= 0.0 && t >= itchAt)
            set ("frames", (float) itch2);
        buffer.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            buffer.copyFrom (c, 0, in, c, at, n);
        processor.processBlock (buffer, midi);
#if HAS_RIG
        if (roundTrip)
            processor.rigTick ((at + n) / sr);
#endif
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
                out.writeFloat (buffer.getSample (c, i));
    }
#if HAS_RIG
    if (roundTrip)
    {
        juce::MemoryBlock bytes;
        processor.getStateInformation (bytes);
        DendriteProcessor again;
        again.setStateInformation (bytes.getData(), (int) bytes.getSize());
        const auto& a = processor.recording;
        const auto& b = again.recording;
        int scarDiff = 0, exposureDiff = 0, scarred = 0;
        for (int k = 0; k < dendrite::rig::Recording::neurons; ++k)
        {
            scarDiff += a.scars (k) != b.scars (k) ? 1 : 0;
            exposureDiff += a.exposure (k) != b.exposure (k) ? 1 : 0;
            scarred += a.scars (k) > 0 ? 1 : 0;
        }
        std::printf ("round trip: %d scars made, %d neurons scarred, state %d bytes; after loading: scars made %d, %d neurons differ in scars, %d in exposure, history %.1f s\n",
                     a.scarTotal(), scarred, (int) bytes.getSize(), b.scarTotal(), scarDiff, exposureDiff, b.restoredSeconds());
        if (scarDiff != 0 || exposureDiff != 0 || a.scarTotal() != b.scarTotal())
            return 5;
    }
#endif
    if (const char* outState = std::getenv ("DENDRITE_STATE_OUT"))
    {
        juce::MemoryBlock bytes;
        processor.getStateInformation (bytes);
        juce::File (juce::String (outState)).replaceWithData (bytes.getData(), bytes.getSize());
    }
    return 0;
}
