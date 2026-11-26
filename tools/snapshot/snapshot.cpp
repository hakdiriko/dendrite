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
//   DENDRITE_YAW=30 DENDRITE_PITCH=-10 the brain's turn, in degrees
//   DENDRITE_ZOOM=4,0.3,0.6            the magnification, and the field's centre as fractions of the box
//   DENDRITE_DRAG=x0,y0,x1,y1          before playing, the hand drags across the specimen (window pixels)
//   DENDRITE_SCRIPT=file               with DENDRITE_FRAMES: the hand, one event a line, "t kind x y [more]" in window pixels:
//                                      down/drag/up (more: left, right, alt, shift; up: click = not a drag), move, wheel (more: notches);
//                                      played at t seconds, on whichever part of the window is under the hand
//   DENDRITE_TURN=deg                  with DENDRITE_FRAMES: the yaw turns this many degrees a second
//   DENDRITE_PLAY=bpm                  a host transport playing from bar 1 at this tempo, in 4/4
//   DENDRITE_SIDECHAIN=kick.wav        the sidechain bus on, fed from that file (looped)
//   DENDRITE_REFERENCE=dnp01           3.6: the reference open on a term or a cell type (the focus)
//   DENDRITE_SEARCH=text               3.6: the search open with this typed
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
    // DENDRITE_SIDECHAIN=kick.wav: the sidechain bus on, stereo, fed from that file (looped)
    std::unique_ptr<juce::AudioFormatReader> sideReader;
    if (env ("DENDRITE_SIDECHAIN").isNotEmpty())
    {
        sideReader.reset (formats.createReaderFor (juce::File (env ("DENDRITE_SIDECHAIN"))));
        auto layout = processor.getBusesLayout();
        if (sideReader == nullptr || layout.inputBuses.size() < 2)
            return 6;
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        if (! processor.setBusesLayout (layout))
            return 6;
        processor.setRateAndBufferSizeDetails (reader->sampleRate, 512);
    }
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
    if (const auto z = env ("DENDRITE_ZOOM"); z.isNotEmpty())
    {
        juce::StringArray v;
        v.addTokens (z, ",", "");
        processor.recording.zoom = v[0].getFloatValue();
        processor.recording.zoomX = v.size() > 1 ? v[1].getFloatValue() : 0.5f;
        processor.recording.zoomY = v.size() > 2 ? v[2].getFloatValue() : 0.5f;
    }
    const float turn = env ("DENDRITE_TURN").getFloatValue();
    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
    auto* editor = dynamic_cast<DendriteEditor*> (base.get());
    editor->setSize ((int) (DendriteEditor::designWidth * scale), (int) (DendriteEditor::designHeight * scale));
    if (const auto size = env ("DENDRITE_SIZE"); size.isNotEmpty()) // w,h in pixels: any window shape
        editor->setSize (size.upToFirstOccurrenceOf (",", false, false).getIntValue(), size.fromFirstOccurrenceOf (",", false, false).getIntValue());
    if (const auto drag = env ("DENDRITE_DRAG"); drag.isNotEmpty())
    {
        juce::StringArray v;
        v.addTokens (drag, ",", "");
        for (auto* c : editor->getChildren())
            if (dynamic_cast<dendrite::ui::Specimen*> (c) != nullptr)
            {
                auto& src = *juce::Desktop::getInstance().getMouseSource (0);
                const auto a = c->getLocalPoint (editor, juce::Point<float> (v[0].getFloatValue(), v[1].getFloatValue()));
                const auto b = c->getLocalPoint (editor, juce::Point<float> (v[2].getFloatValue(), v[3].getFloatValue()));
                auto ev = [&] (juce::Point<float> p, bool dragged) {
                    return juce::MouseEvent (src, p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             c, c, juce::Time::getCurrentTime(), a, juce::Time::getCurrentTime(), 1, dragged);
                };
                c->mouseDown (ev (a, false));
                for (int k = 1; k <= 10; ++k)
                    c->mouseDrag (ev (a + (b - a) * ((float) k / 10.0f), true));
                c->mouseUp (ev (b, true));
            }
    }
    if (env ("DENDRITE_NO_ELECTRODE").isNotEmpty() || env ("DENDRITE_GEOMETRY").isNotEmpty())
        for (auto* c : editor->getChildren())
            if (auto* s = dynamic_cast<dendrite::ui::Specimen*> (c))
            {
                if (env ("DENDRITE_NO_ELECTRODE").isNotEmpty())
                    s->hideElectrodeForTools();
                const auto b = s->brainArea() + s->getPosition().toFloat(); // the plate at 1x, in window pixels
                std::printf ("brain %.2f %.2f %.2f %.2f\n", b.getX(), b.getY(), b.getWidth(), b.getHeight());
            }
    if (const auto what = env ("DENDRITE_REFERENCE"); what.isNotEmpty())
        editor->referenceForTools (what, false);
    if (const auto what = env ("DENDRITE_SEARCH"); what.isNotEmpty())
        editor->referenceForTools (what, true);
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

    // the scripted hand: real mouse events through the specimen's own handlers
    struct Act { double t; juce::String kind; juce::Point<float> at; juce::String more; };
    std::vector<Act> script;
    juce::Component* specimen = nullptr;
    for (auto* c : editor->getChildren())
        if (dynamic_cast<dendrite::ui::Specimen*> (c) != nullptr)
            specimen = c;
    if (const auto file = env ("DENDRITE_SCRIPT"); file.isNotEmpty())
    {
        juce::StringArray lines;
        juce::File (file).readLines (lines);
        for (auto l : lines)
        {
            juce::StringArray v;
            v.addTokens (l.trim(), " ", "");
            v.removeEmptyStrings();
            if (v.size() >= 4)
                script.push_back ({ v[0].getDoubleValue(), v[1], { v[2].getFloatValue(), v[3].getFloatValue() }, v.size() > 4 ? v[4] : juce::String() });
        }
    }
    size_t nextAct = 0;
    juce::Point<float> downAt;
    juce::ModifierKeys held;
    juce::Component* target = nullptr; // the window's part under the hand (the specimen, the trace, the settings line)
    auto play = [&] (const Act& a) {
        auto& src = *juce::Desktop::getInstance().getMouseSource (0);
        if (a.kind == "down" || a.kind == "move" || a.kind == "wheel" || target == nullptr)
        {
            target = specimen;
            for (auto* c : editor->getChildren())
                if (c->getBounds().toFloat().contains (a.at))
                    target = c;
        }
        auto* specimen = target;
        const auto p = specimen->getLocalPoint (editor, a.at);
        if (a.kind == "down")
        {
            int flags = a.more.contains ("right") ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
            if (a.more.contains ("alt")) flags |= juce::ModifierKeys::altModifier;
            if (a.more.contains ("shift")) flags |= juce::ModifierKeys::shiftModifier;
            held = juce::ModifierKeys (flags);
            downAt = p;
        }
        const auto ev = juce::MouseEvent (src, p, held, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, specimen, specimen, juce::Time::getCurrentTime(), downAt,
                                          juce::Time::getCurrentTime(), 1, a.kind != "down" && ! a.more.contains ("click"));
        if (a.kind == "down") specimen->mouseDown (ev);
        else if (a.kind == "drag") specimen->mouseDrag (ev);
        else if (a.kind == "up") { specimen->mouseUp (ev); held = {}; }
        else if (a.kind == "move") specimen->mouseMove (ev);
        else if (a.kind == "wheel")
        {
            juce::MouseWheelDetails w {};
            w.deltaY = a.more.getFloatValue() * 0.25f;
            specimen->mouseWheelMove (ev, w);
        }
    };

    juce::AudioBuffer<float> buffer (std::max (2, processor.getTotalNumInputChannels()), 512);
    juce::AudioBuffer<float> side (2, 512);
    juce::MidiBuffer midi;
    const auto total = (juce::int64) (seconds * reader->sampleRate);
    double t = 0.0, nextFrame = framesFrom;
    int frameNumber = 0;
    for (juce::int64 pos = 0; pos < total; pos += 512)
    {
        buffer.clear();
        reader->read (&buffer, 0, 512, pos % std::max<juce::int64> (1, reader->lengthInSamples - 512), true, true);
        if (sideReader != nullptr && buffer.getNumChannels() >= 4)
        {
            side.clear();
            sideReader->read (&side, 0, 512, pos % std::max<juce::int64> (1, sideReader->lengthInSamples - 512), true, true);
            buffer.copyFrom (2, 0, side, 0, 0, 512);
            buffer.copyFrom (3, 0, side, 1, 0, 512);
        }
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
            while (specimen != nullptr && nextAct < script.size() && script[nextAct].t <= now)
                play (script[nextAct++]);
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
