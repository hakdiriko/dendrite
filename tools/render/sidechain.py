"""Material for the sidechain's A/B renders (3.4): a kick to send dendrite, and three beds to put
dendrite on, all 16 bars at 120 BPM and 48 kHz, then 4 bars of silence for the tails.

   python3 tools/render/material.py MAT        (first: pad, chords and voice)
   python3 tools/render/sidechain.py MAT OUT

kick.wav   bars 1-4 four on the floor, a 16th fill at the end of bar 4; bars 5-8 the same with
           ghost notes; bars 9-10 a breakdown (no kick); bars 11-16 the kick comes back
pad.wav, keys.wav, voice.wav   the beds (material.py's pad, chords and voice, tiled to 16 bars)
"""
import sys, os
import numpy as np
import soundfile as sf

sr = 48000; bpm = 120.0; beat = 60 / bpm; bar = 4 * beat
mat, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
N = int(20 * bar * sr)


def kick(level=1.0):
    n = int(0.45 * sr); t = np.arange(n) / sr
    f = 48 + 110 * np.exp(-t / 0.035)
    ph = 2 * np.pi * np.cumsum(f) / sr
    body = np.sin(ph) * np.exp(-t / 0.22)
    click = np.random.default_rng(3).standard_normal(n) * np.exp(-t / 0.0015) * 0.3
    return level * (body + click) * (1 - np.exp(-t / 0.0004))


k = np.zeros(N)
def put(at_beats, level=1.0):
    i = int(round(at_beats * beat * sr)); s = kick(level); m = min(len(s), N - i)
    k[i:i + m] += s[:m]

for b in range(16):
    if b in (8, 9):
        continue                                 # the breakdown
    for q in range(4):
        if b == 3 and q == 3:
            for s16 in range(4):                 # the fill: 16ths on the last beat of bar 4
                put(b * 4 + 3 + s16 * 0.25, 0.8)
            continue
        put(b * 4 + q)
    if 4 <= b <= 7:                              # ghost notes, 14 dB down
        put(b * 4 + 1.75, 0.2)
        put(b * 4 + 3.5, 0.2)
k = k / np.max(np.abs(k)) * 0.9
sf.write(f"{out}/kick.wav", np.vstack([k, k]).T.astype(np.float32), sr, subtype="FLOAT")


def bed(name, src, bars):
    x, r = sf.read(f"{mat}/{src}.wav", always_2d=True)
    assert r == sr
    one = x[:int(bars * bar * sr)]
    y = np.zeros((N, 2))
    pos = 0
    while pos < int(16 * bar * sr):
        m = min(len(one), int(16 * bar * sr) - pos)
        y[pos:pos + m] += one[:m]
        pos += m
    fade = int(0.05 * sr)                        # no click where the 16 bars end
    end = int(16 * bar * sr)
    y[end - fade:end] *= np.linspace(1, 0, fade)[:, None]
    y[end:] = 0
    sf.write(f"{out}/{name}.wav", y.astype(np.float32), sr, subtype="FLOAT")


bed("pad", "pad", 8)
bed("keys", "chords", 8)
bed("voice", "voice", 8)
print("ok")
