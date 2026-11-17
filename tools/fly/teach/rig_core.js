// The teach rig in JavaScript: wraps rig.wasm (tools/fly/teach/teach_wasm.cpp). Shared by the
// page's AudioWorklet and by tools/fly/teach/check_wasm.js (node), so the test runs this exact code.
// Every message is applied at the start of a 128-sample render quantum and logged with that
// sample (counted from the take's first sample), so a take can be replayed sample-exactly.
class TeachRig {
  constructor(module) {
    const self = this;
    const imports = {
      env: { emscripten_notify_memory_growth() { self.views(); } },
      wasi_snapshot_preview1: { fd_close: () => 0, fd_write: () => 0, fd_seek: () => 0 },
    };
    this.x = new WebAssembly.Instance(module, imports).exports;
    if (this.x._initialize) this.x._initialize();
    this.views();
    this.frame = 0;      // samples since the take started
    this.playing = false;
    this.log = [];       // [sample, action, value...] applied since the last drain
    this.starts = [];    // [beat, move] since the last drain
  }
  views() { this.f32 = new Float32Array(this.x.memory.buffer); }
  init(sampleRate) { this.sr = sampleRate; this.x.rig_init(sampleRate); this.views(); }
  setLoop(left, right, bpm, barBeats) {
    const n = left.length;
    const ptr = this.x.rig_loop_buffer(n);
    this.views();
    this.f32.set(left, ptr >> 2);
    this.f32.set(right, (ptr >> 2) + n);
    this.x.rig_set_loop(n, bpm, barBeats);
  }
  // m: {a: action, v: value(s)}
  apply(m) {
    const x = this.x;
    switch (m.a) {
      case 'take':  // a new take, v = [feed, space, time]: a freshly prepared engine, beat 0 now, the loop from its start
        x.rig_set_controls(m.v[0], m.v[1], m.v[2]);
        x.rig_init(this.sr); this.views();
        x.rig_play(1); this.frame = 0; this.playing = true;
        this.log = [[0, 'controls', ...m.v], [0, 'play', 1]];
        this.starts = [];
        return;
      case 'stop': x.rig_play(0); this.playing = false; break;
      case 'cue': x.rig_cue(m.v); break;
      case 'throw': x.rig_throw(m.v ? 1 : 0); break;
      case 'freeze': x.rig_freeze(m.v ? 1 : 0); break;
      case 'controls': x.rig_set_controls(m.v[0], m.v[1], m.v[2]); break;
      default: return;
    }
    const value = m.a === 'stop' ? [0] : Array.isArray(m.v) ? m.v : [m.v ? +m.v : 0];
    this.log.push([this.frame, m.a === 'stop' ? 'play' : m.a, ...value]);
  }
  // one render quantum (128 samples) into two Float32Arrays
  render(left, right) {
    const n = left.length, x = this.x;
    x.rig_process(n);
    if (this.f32.buffer !== x.memory.buffer) this.views();
    const l = x.rig_out_left() >> 2, r = x.rig_out_right() >> 2;
    left.set(this.f32.subarray(l, l + n));
    right.set(this.f32.subarray(r, r + n));
    const c = x.rig_started_count();
    for (let i = 0; i < c; i++) this.starts.push([x.rig_started_beat(i), x.rig_started_move(i)]);
    if (c) x.rig_started_clear();
    this.frame += n;
  }
  state() {
    const x = this.x;
    return { frame: this.frame, beat: x.rig_beat(), moving: x.rig_moving(), waiting: x.rig_waiting(),
             throwing: x.rig_throwing(), frozen: x.rig_frozen(), playing: this.playing };
  }
}
if (typeof module !== 'undefined') module.exports = { TeachRig };
