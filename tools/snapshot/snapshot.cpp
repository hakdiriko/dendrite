// Renders dendrite's window to a PNG while audio plays through it, so the look can be
// checked without a host. Local tool, not built in CI.
//   dendrite_snapshot material.wav out.png [feed] [space] [itch] [seconds] [scale] [throwAt]
// Optional environment:
//   DENDRITE_THROW_LEN=0.6             how long the throw is held, in seconds
//   DENDRITE_SIZE=w,h                  the window in logical pixels
//   DENDRITE_PNG_SCALE=1               the picture's pixels per logical pixel (2: a 200% screen)
//   DENDRITE_FRAMES=dir                also write frames at 30 fps (frame_00000.png ...)
//   DENDRITE_FRAMES_FROM=t             ... starting at t seconds
//   DENDRITE_AUDIO=out.wav             also write what came out of the plug-in
//   DENDRITE_KEEP_OUT=dir              at the end, write the last finished itch as keep would drag it
//   DENDRITE_TIME=1                    how long one frame of the window takes to paint
//   DENDRITE_HOVER=x,y                 the hand rests over the specimen there (window pixels)
//   DENDRITE_IMAGING=two-photon        the imaging method (trace, two-photon, voltage, photons, tracts, 2-dg)
//   DENDRITE_YAW=30 DENDRITE_PITCH=-10 the goniometer's angles, in degrees
//   DENDRITE_TURN=deg                  with DENDRITE_FRAMES: the yaw turns this many degrees a second
//   DENDRITE_PLAY=bpm                  a host transport playing from bar 1 at this tempo, in 4/4
#include <juce_audio_formats/juce_audio_formats.h>
#include "Processor.h"
#include "Editor.h"

namespace
{
juce::String env (const char* name)
{
    const char* v = std::getenv (name);
    return v != nullptr ? juce::String (v) : juce::String();
}

// a host's transport, playing from the start in 4/4
struct Transport : juce::AudioPlayHead
{
    double bpm = 120.0, sampleRate = 48000.0;
    juce::int64 position = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setIsPlaying (true);
        info.setTimeInSamples (position);
        info.setPpqPosition ((double) position / sampleRate * bpm / 60.0);
        return info;
    }
};

bool writePng (juce::Component& c, const juce::File& out, float scale)
{
    auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    return png.writeImageToStream (image, stream);
}
} // namespace

int main (int argc, char** argv)
{
        juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::printf ("usage: dendrite_snapshot material.wav out.png [feed] [space] [itch] [seconds] [scale] [throwAt]\n");
        return 1;
    }
    const float feed = argc > 3 ? (float) std::atof (argv[3]) : DendriteProcessor::defaultFeed;
    const float space = argc > 4 ? (float) std::atof (argv[4]) : DendriteProcessor::defaultSpace;
    const int itch = argc > 5 ? std::atoi (argv[5]) : DendriteProcessor::defaultItch;
    const double seconds = argc > 6 ? std::atof (argv[6]) : 6.0;
    const float scale = argc > 7 ? (float) std::atof (argv[7]) : 1.0f;
    const double throwAt = argc > 8 ? std::atof (argv[8]) : -1.0;
    const double throwLen = env ("DENDRITE_THROW_LEN").isNotEmpty() ? env ("DENDRITE_THROW_LEN").getDoubleValue() : 0.6;
    const juce::File framesDir = env ("DENDRITE_FRAMES").isNotEmpty() ? juce::File (env ("DENDRITE_FRAMES")) : juce::File();
    const double framesFrom = env ("DENDRITE_FRAMES_FROM").getDoubleValue();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File { juce::String (argv[1]) }));
    if (reader == nullptr)
        return 2;

    DendriteProcessor processor;
    processor.setPlayConfigDetails (2, 2, reader->sampleRate, 512);
    processor.prepareToPlay (reader->sampleRate, 512);
    Transport transport;
    transport.sampleRate = reader->sampleRate;
    if (env ("DENDRITE_PLAY").isNotEmpty())
    {
        transport.bpm = env ("DENDRITE_PLAY").getDoubleValue();
        processor.setPlayHead (&transport);
    }
    processor.params.getParameter ("feed")->setValueNotifyingHost (feed);
    processor.params.getParameter ("space")->setValueNotifyingHost (space);
    processor.params.getParameter ("frames")->setValueNotifyingHost (processor.params.getParameter ("frames")->convertTo0to1 ((float) itch));

    if (const auto m = env ("DENDRITE_IMAGING"); m.isNotEmpty())
        for (int k = 0; k < dendrite::rig::Recording::methods; ++k)
            if (m == dendrite::rig::Recording::methodName (k))
                processor.recording.method = k;
    processor.recording.yaw = env ("DENDRITE_YAW").getFloatValue();
    processor.recording.pitch = env ("DENDRITE_PITCH").getFloatValue();
    const float turn = env ("DENDRITE_TURN").getFloatValue();
    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
    auto* editor = dynamic_cast<DendriteEditor*> (base.get());
    editor->setSize ((int) (DendriteEditor::designWidth * scale), (int) (DendriteEditor::designHeight * scale));
    if (const auto size = env ("DENDRITE_SIZE"); size.isNotEmpty()) // w,h in pixels: any window shape
        editor->setSize (size.upToFirstOccurrenceOf (",", false, false).getIntValue(), size.fromFirstOccurrenceOf (",", false, false).getIntValue());
    if (const auto hover = env ("DENDRITE_HOVER"); hover.isNotEmpty())
        for (auto* c : editor->getChildren())
            if (auto* s = dynamic_cast<dendrite::ui::Specimen*> (c))
                s->hoverForTools (c->getLocalPoint (editor, juce::Point<float> (hover.upToFirstOccurrenceOf (",", false, false).getFloatValue(),
                                                                                 hover.fromFirstOccurrenceOf (",", false, false).getFloatValue())));

    std::unique_ptr<juce::AudioFormatWriter> audioOut;
    if (env ("DENDRITE_AUDIO").isNotEmpty())
    {
        const juce::File f (env ("DENDRITE_AUDIO"));
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = f.createOutputStream();
        if (stream != nullptr)
            audioOut = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (reader->sampleRate)
                                                        .withNumChannels (2).withBitsPerSample (24));
    }
    if (framesDir != juce::File())
        framesDir.createDirectory();

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto total = (juce::int64) (seconds * reader->sampleRate);
    double t = 0.0, nextFrame = framesFrom;
    int frameNumber = 0;
    for (juce::int64 pos = 0; pos < total; pos += 512)
    {
        buffer.clear();
        reader->read (&buffer, 0, 512, pos % std::max<juce::int64> (1, reader->lengthInSamples - 512), true, true);
        const double now = (double) pos / reader->sampleRate;
        const bool throwing = throwAt >= 0.0 && now >= throwAt && now < throwAt + throwLen;
        processor.params.getParameter ("throw")->setValueNotifyingHost (throwing ? 1.0f : 0.0f);
        transport.position = pos;
        processor.processBlock (buffer, midi);
        if (audioOut != nullptr)
            audioOut->writeFromAudioSampleBuffer (buffer, 0, 512);
        if (now - t >= 1.0 / 30.0)
        {
            t = now;
            processor.rigTick (now); // the recorders, as the processor's timer would
            if (turn != 0.0f)
            {
                float yaw = env ("DENDRITE_YAW").getFloatValue() + turn * (float) (now - framesFrom);
                processor.recording.yaw = yaw - 360.0f * std::round (yaw / 360.0f);
            }
            editor->animateTo (now);
        }
        if (framesDir != juce::File() && now >= nextFrame)
        {
            nextFrame += 1.0 / 30.0;
            writePng (*editor, framesDir.getChildFile (juce::String::formatted ("frame_%05d.png", frameNumber++)), 1.0f);
        }
    }
    processor.rigTick (t + 0.001);
    editor->animateTo (t + 0.001);
    if (env ("DENDRITE_KEEP_OUT").isNotEmpty())
    {
        processor.keep.collect();
        const auto& d = processor.keep.diary();
        for (auto it = d.rbegin(); it != d.rend(); ++it)
            if (it->kind == dendrite::keep::Kind::itch && it->endSample >= 0)
            {
                if (auto take = processor.keep.takeMark (*it, false))
                    std::printf ("kept %s\n", processor.keep.write (*take, juce::File (env ("DENDRITE_KEEP_OUT"))).getFullPathName().toRawUTF8());
                break;
            }
    }
    if (env ("DENDRITE_TIME").isNotEmpty())
    {
        // how long one frame of the window takes to tick and paint, in software
        juce::Image img (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        const double start = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 120; ++k)
        {
            editor->animateTo (t + 0.001 + k / 60.0);
            juce::Graphics g (img);
            editor->paintEntireComponent (g, true);
        }
        const double total = (juce::Time::getMillisecondCounterHiRes() - start) / 120.0;
        const double tickStart = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 120; ++k)
            editor->animateTo (t + 2.001 + k / 60.0);
        std::printf ("%.2f ms per frame (%.2f ms of it simulation)\n", total, (juce::Time::getMillisecondCounterHiRes() - tickStart) / 120.0);
    }
    if (env ("DENDRITE_RATES").isNotEmpty())
    {
        // how fast the neurons fire now (the trace over its time constant), as quantiles
        std::vector<float> hz;
        for (int n = 0; n < dendrite::rig::Recording::neurons; ++n)
            hz.push_back (processor.recording.trace (n) / (float) dendrite::rig::Recording::traceSeconds);
        std::sort (hz.begin(), hz.end());
        for (double q : { 0.1, 0.25, 0.5, 0.75, 0.9, 0.99 })
            std::printf ("q%.2f %.1f Hz\n", q, hz[(size_t) (q * (double) (hz.size() - 1))]);
        std::printf ("cut brain %.0f spikes/s\n", processor.recording.spikesPerSecond());
    }
    // DENDRITE_PNG_SCALE=1: the window as a 100% Windows screen shows it
    const float pngScale = env ("DENDRITE_PNG_SCALE").isNotEmpty() ? env ("DENDRITE_PNG_SCALE").getFloatValue() : 2.0f;
    return writePng (*editor, juce::File { juce::String (argv[2]) }, pngScale) ? 0 : 3;
}
