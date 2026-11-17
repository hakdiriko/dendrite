// Checks that the teach page's WebAssembly rig sounds like the native one: plays a scripted take
// through rig.wasm with rig_core.js (the page's own code), writes its log the way teach.py does,
// replays the log natively, and compares the audio and the beats the moves started on. Also
// reports how fast the wasm runs (the page needs well over 1x real time).
//   node tools/fly/teach/check_wasm.js build/teach/rig.wasm build/teach_replay loop.wav
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');
const { TeachRig } = require('./rig_core.js');
const { readWav } = require('./wav.js');

const [wasmPath, replayPath, wavPath] = process.argv.slice(2);
const loop = readWav(fs.readFileSync(wavPath));
const bpm = 127, bar = 4;
const rig = new TeachRig(new WebAssembly.Module(fs.readFileSync(wasmPath)));
rig.init(loop.sampleRate);
rig.setLoop(loop.left, loop.right, bpm, bar);

const script = { 0: [{ a: 'take', v: [0.5, 0.4, 0] }], 100: [{ a: 'cue', v: 1 }], 400: [{ a: 'cue', v: 2 }],
  700: [{ a: 'throw', v: 1 }], 760: [{ a: 'throw', v: 0 }], 900: [{ a: 'freeze', v: 1 }], 1000: [{ a: 'freeze', v: 0 }],
  1100: [{ a: 'controls', v: [0.7, 0.2, 1] }], 1200: [{ a: 'cue', v: 4 }], 1700: [{ a: 'cue', v: 5 }],
  2200: [{ a: 'cue', v: 7 }] /* replaces the rush still waiting for the dive to end */, 2500: [{ a: 'cue', v: 6 }], 2800: [{ a: 'cue', v: 3 }], 3000: [{ a: 'stop' }] };
const quanta = 3400, q = 128;
const outL = new Float32Array(quanta * q), outR = new Float32Array(quanta * q);
const l = new Float32Array(q), r = new Float32Array(q);
const t0 = process.hrtime.bigint();
for (let k = 0; k < quanta; k++) {
  for (const m of script[k] || []) rig.apply(m);
  rig.render(l, r);
  outL.set(l, k * q); outR.set(r, k * q);
}
const seconds = Number(process.hrtime.bigint() - t0) / 1e9;
const audio = quanta * q / loop.sampleRate;
console.log(`wasm: ${audio.toFixed(1)} s of audio in ${seconds.toFixed(2)} s (${(audio / seconds).toFixed(0)}x real time)`);

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'teach-'));
const loopFile = path.join(dir, 'loop.f32'), evFile = path.join(dir, 'events.txt'), outFile = path.join(dir, 'out.f32');
const both = new Float32Array(loop.left.length * 2);
both.set(loop.left); both.set(loop.right, loop.left.length);
fs.writeFileSync(loopFile, Buffer.from(both.buffer));
fs.writeFileSync(evFile, rig.log.map(e => e.join(' ')).join('\n') + '\n');
const printed = execFileSync(replayPath, [loop.sampleRate, bpm, bar, quanta * q, loopFile, evFile, outFile]).toString();
const native = new Float32Array(fs.readFileSync(outFile).buffer.slice(0));
let worst = 0, peak = 0;
for (let i = 0; i < quanta * q; i++) {
  worst = Math.max(worst, Math.abs(native[2 * i] - outL[i]), Math.abs(native[2 * i + 1] - outR[i]));
  peak = Math.max(peak, Math.abs(outL[i]));
}
const names = ['none', 'hop', 'leap', 'duck', 'dive', 'rush', 'hold', 'sink'];
const wasmStarts = rig.starts.map(([b, m]) => `start ${b.toFixed(4)} ${names[m]}`).join('\n');
console.log(wasmStarts);
const same = wasmStarts.trim() === printed.trim();
console.log(`moves: ${rig.starts.length}, same starts as native: ${same}`);
console.log(`largest difference from native: ${worst.toExponential(2)} (peak ${peak.toFixed(3)}, ${(20 * Math.log10(worst / peak + 1e-12)).toFixed(0)} dB below)`);
fs.rmSync(dir, { recursive: true });
process.exit(same && worst / peak < 1e-3 && rig.starts.length === 6 ? 0 : 1);
