# The teach page (phase 2)

Hakan plays the fly's moves by hand on dendrite's own echo, over loops. What he plays, and where he
leaves the music alone, is the teaching data for phase 4 (the mushroom body taught by dopamine from
his playing). Plan: "The fly: second review and plan" (https://claude.ai/artifact/VAQ3CJekF5gEyhkWymCpKk).

- `Source/dsp/FlyBody.h`: the fly's body without its brain. The seven tape moves of 2.4.3 (hop, leap,
  duck, dive, rush, hold, sink) with the brain's choices fixed, on the song's grid (small moves on the
  next 16th, big ones on the next half bar, as the fly does wide awake), plus throw and freeze while held.
  The fly still has its own copy in `Fly::startMove`; phase 4 makes it use this one.
- `rig.h`: the engine (the plug-in's echo and output chain, itch off) with the body driven per sample
  and the touch handed to the echo every 16 samples.
- `teach_wasm.cpp`, `build_wasm.sh`: the rig as a 158 KB WebAssembly module (no JS glue; the fly's
  brain data is replaced by `flydata_stub.cpp`, since the page never runs it).
- `rig_core.js`, `wav.js`, `page.html`, `make_page.py`: the page. It runs the rig in an AudioWorklet
  (main-thread fallback), logs every key at the sample it took hold, saves each take to the artifact's
  store (collection `takes`, the gate answer in `verdicts`) and can download them all as one JSON.
- `replay.cpp`, `teach.py`: replay a take natively and check the moves started on the beats the page
  logged; progress against the data target.
- `check_wasm.js`: the wasm rig against the native one (same move beats, audio within -79 dB, speed).

```
source /path/to/emsdk/emsdk_env.sh
tools/fly/teach/build_wasm.sh                     # build/teach/rig.wasm + build/teach/teach.html
g++ -O2 -std=c++20 -I Source -I tools/fly/teach tools/fly/teach/replay.cpp \
    $(ls Source/dsp/*.cpp | grep -v FlyData.cpp) tools/fly/teach/flydata_stub.cpp -o build/teach_replay
node tools/fly/teach/check_wasm.js build/teach/rig.wasm build/teach_replay tools/fly/taste/loops/fly-keys_127bpm.wav
python3 tools/fly/teach/teach.py stats takes.json
python3 tools/fly/teach/teach.py replay takes.json 0 loop.wav out.wav
```

Takes come back from the page's store with the ArtifactData tool (`list` of collection `takes`), or
from its "Download all takes" file. A take is sample-exact to replay when the loop was a WAV played at
its own sample rate (the page opens the audio device at the loop's rate); other formats are converted
to 48 kHz and replay approximately.

Gate (from the plan): Hakan says it feels like playing the plug-in. Data target: ten pieces, 32 bars
each, two takes, at least 300 moves. Two pieces stay out of teaching as the exam.
