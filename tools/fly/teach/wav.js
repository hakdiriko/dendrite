// A small WAV reader (PCM 16/24/32-bit integer, 32-bit float; mono or stereo), shared by the teach
// page and its node check, so both read a loop to the same floats.
function readWav(bytes) {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  const tag = (o) => String.fromCharCode(u8[o], u8[o + 1], u8[o + 2], u8[o + 3]);
  if (tag(0) !== 'RIFF' || tag(8) !== 'WAVE') throw new Error('not a WAV file');
  let fmt = null, data = null;
  for (let o = 12; o + 8 <= u8.length;) {
    const id = tag(o), size = dv.getUint32(o + 4, true);
    if (id === 'fmt ') fmt = { format: dv.getUint16(o + 8, true), channels: dv.getUint16(o + 10, true),
                               sampleRate: dv.getUint32(o + 12, true), bits: dv.getUint16(o + 22, true),
                               sub: size >= 26 ? dv.getUint16(o + 32, true) : 0 };
    if (id === 'data') data = { at: o + 8, size: Math.min(size, u8.length - o - 8) };
    o += 8 + size + (size & 1);
  }
  if (!fmt || !data) throw new Error('WAV without fmt or data');
  const bytesPer = fmt.bits / 8, frames = Math.floor(data.size / (bytesPer * fmt.channels));
  const left = new Float32Array(frames), right = new Float32Array(frames);
  const float = fmt.format === 3 || (fmt.format === 0xfffe && fmt.sub === 3); // extensible: its subformat says
  const read = (o) => {
    if (float) return dv.getFloat32(o, true);
    if (fmt.bits === 16) return dv.getInt16(o, true) / 32768;
    if (fmt.bits === 24) return ((u8[o] | (u8[o + 1] << 8) | (u8[o + 2] << 16)) << 8 >> 8) / 8388608;
    if (fmt.bits === 32) return dv.getInt32(o, true) / 2147483648;
    throw new Error(`${fmt.bits}-bit WAV is not supported`);
  };
  for (let i = 0; i < frames; i++) {
    const o = data.at + i * bytesPer * fmt.channels;
    left[i] = read(o);
    right[i] = fmt.channels > 1 ? read(o + bytesPer) : left[i];
  }
  return { sampleRate: fmt.sampleRate, left, right };
}
if (typeof module !== 'undefined') module.exports = { readWav };
