// The teach rig: dendrite's engine (the plug-in's own echo and output chain) with the itch off
// and the fly's body played by a hand. The teach page runs it in WebAssembly (teach_wasm.cpp);
// the replay tool runs it natively on a saved session (replay.cpp), so what was heard can be
// rendered again exactly.
//
// The song's clock starts at 0 with the first sample of the loop: beat 0 is the loop's downbeat.
#pragma once

#include "dsp/Engine.h"
#include "dsp/FlyBody.h"

#include <vector>

namespace teach
{
struct Started
{
    int move = 0;
    double beat = 0.0;
};

class Rig
{
public:
    static constexpr int chunk = 16; // the body runs per sample; the echo takes its touch every 16 samples (a third of a ms)

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        engine.useFly (false);
        engine.useHand (false);
        engine.useDice (false);
        engine.setControls (feed, space, false, 0, false);
        engine.setStyle (timing, false);
        engine.prepare (sr);
        body.prepare (sr);
        engine.touchFromOutside (true, dendrite::Touch {});
        restart();
    }

    void setTempo (double bpm, double barBeats) noexcept
    {
        tempo = bpm;
        bar = barBeats;
    }
    void setControls (float f, float s, int t) noexcept
    {
        feed = f;
        space = s;
        timing = t;
    }
    // where the loop is played from: the caller owns the memory (left and right, one pass each)
    void setLoop (const float* l, const float* r, size_t length) noexcept
    {
        loopL = l;
        loopR = r;
        loopLength = length;
        loopAt = 0;
    }
    void play (bool on) noexcept { playing = on; }
    // back to beat 0, the echo emptied
    void restart() noexcept
    {
        engine.reset();
        body.reset();
        samples = 0;
        loopAt = 0;
        started.clear();
    }

    dendrite::FlyBody& hand() noexcept { return body; }
    double beat() const noexcept { return (double) samples * tempo / 60.0 / sr; }
    double bpm() const noexcept { return tempo; }
    // the moves that started since the last clear
    std::vector<Started> started;

    // n samples into left/right (overwritten)
    void process (float* left, float* right, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const bool on = playing && loopLength > 0;
            left[i] = on ? loopL[loopAt] : 0.0f;
            right[i] = on ? loopR[loopAt] : 0.0f;
            if (on && ++loopAt >= loopLength)
                loopAt = 0;
        }
        for (int at = 0; at < n; at += chunk)
        {
            const int m = std::min (chunk, n - at);
            dendrite::Touch t;
            for (int i = 0; i < m; ++i)
            {
                body.setClock ((double) (samples + i) * tempo / 60.0 / sr, tempo, bar);
                t = body.process();
                double b = 0.0;
                if (const int s = body.takeStarted (b); s != 0 && started.size() < 64)
                    started.push_back ({ s, b });
            }
            engine.touchFromOutside (true, t);
            engine.setControls (feed, space, false, 0, false);
            engine.setStyle (timing, false);
            dendrite::HostTime time;
            time.bpm = tempo;
            time.barBeats = bar;
            time.playing = true;
            time.ppq = beat();
            engine.process (left + at, right + at, m, time);
            samples += (uint64_t) m;
        }
    }

private:
    float sr = 48000.0f, feed = 0.5f, space = 0.4f;
    int timing = 0;
    double tempo = 120.0, bar = 4.0;
    const float* loopL = nullptr;
    const float* loopR = nullptr;
    size_t loopLength = 0, loopAt = 0;
    bool playing = false;
    uint64_t samples = 0;
    dendrite::Engine engine;
    dendrite::FlyBody body;
};
} // namespace teach
