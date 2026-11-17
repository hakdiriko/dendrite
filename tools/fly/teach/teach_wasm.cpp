// The teach rig as a WebAssembly module, for tools/fly/teach/page (no JS glue: the page's
// AudioWorklet instantiates it directly). Build: tools/fly/teach/build_wasm.sh
#include "rig.h"

#include <cstdlib>

namespace
{
teach::Rig* rig = nullptr;
std::vector<float> loop, outL, outR;
} // namespace

#define EXPORT extern "C" __attribute__ ((used, visibility ("default")))

EXPORT void rig_init (float sampleRate)
{
    if (rig == nullptr)
        rig = new teach::Rig();
    outL.assign (4096, 0.0f);
    outR.assign (4096, 0.0f);
    rig->prepare (sampleRate);
}
// room for a loop of `length` samples per side; the page writes left then right into it
EXPORT float* rig_loop_buffer (int length)
{
    loop.assign ((size_t) length * 2, 0.0f);
    return loop.data();
}
EXPORT void rig_set_loop (int length, double bpm, double barBeats)
{
    rig->setLoop (loop.data(), loop.data() + length, (size_t) length);
    rig->setTempo (bpm, barBeats);
}
EXPORT void rig_set_controls (float feed, float space, int timing) { rig->setControls (feed, space, timing); }
EXPORT void rig_play (int on) { rig->play (on != 0); }
EXPORT void rig_restart() { rig->restart(); }
EXPORT void rig_cue (int move) { rig->hand().cue (move); }
EXPORT void rig_throw (int held) { rig->hand().setThrow (held != 0); }
EXPORT void rig_freeze (int held) { rig->hand().setFreeze (held != 0); }
EXPORT float* rig_out_left() { return outL.data(); }
EXPORT float* rig_out_right() { return outR.data(); }
EXPORT void rig_process (int n) { rig->process (outL.data(), outR.data(), std::min (n, 4096)); }
EXPORT double rig_beat() { return rig->beat(); }
EXPORT int rig_moving() { return rig->hand().moving(); }
EXPORT int rig_waiting() { return rig->hand().waiting(); }
EXPORT int rig_throwing() { return rig->hand().throwing() ? 1 : 0; }
EXPORT int rig_frozen() { return rig->hand().frozen() ? 1 : 0; }
EXPORT int rig_started_count() { return (int) rig->started.size(); }
EXPORT int rig_started_move (int i) { return rig->started[(size_t) i].move; }
EXPORT double rig_started_beat (int i) { return rig->started[(size_t) i].beat; }
EXPORT void rig_started_clear() { rig->started.clear(); }
