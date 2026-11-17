#!/usr/bin/env python3
"""The fly's material set: the pieces every brain-share measurement plays. numpy only, deterministic.

    python3 tools/fly/probe/material.py OUTDIR [--own DIR]

Writes OUTDIR/<name>.wav (48 kHz, 24 bit) and OUTDIR/pieces.json (name, file, bpm, beats per bar,
loop seconds, repeats to reach five minutes). --own DIR adds Hakan's loops from his playground
(C:\\Claude-Playground\\dendrite\\loops): any WAV whose name ends in _<bpm>bpm (and _3-4 or _6-8 for
the bar), copied as they are.

The pieces (all synthetic, made here, which is the set's weakness until his own loops are in):
  keys       the taste rounds' bare keys (Dm7 Gm7 Bb A7 stabs, 2 bars, 127 bpm, no drums)
  groove     the same keys with kick, rim and hats (127)
  dubloop    a 1-bar dub techno loop: kick, offbeat stab, hats (120)
  drums      kick, rim, hats with gaps in bars 4 and 8 (120)
  voice      sung syllables with rests (120)
  pad        a held chord, slowly filtered, no hits at all (120)
  sparse     one dull stab every two bars and little else (96)
  dense      16th hats, four-to-the-floor, 8th bass, claps, an open hat (140)
  sections   a 32-bar passage that changes: groove, bare keys, a break on a pad, groove with keys (127)
  waltz      3/4: bass on one, chords on two and three, hats (92)
  sixeight   6/8 in two dotted beats: kick, rim, a rolling bass (120, 3 quarter notes a bar)
  fast       a broken beat with a sub at 174
"""
import json, os, re, shutil, sys, wave
import numpy as np

SR = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
LOOPS = os.path.join(HERE, '..', 'taste', 'loops')
FIVE_MINUTES = 300.0


def write(path, y):
    y = np.asarray(y, dtype=np.float64)
    if y.ndim == 1:
        y = np.stack([y, y], 1)
    y = y / max(1e-9, np.abs(y).max()) * 0.7
    w = wave.open(path, 'wb')
    w.setnchannels(2); w.setsampwidth(3); w.setframerate(SR)
    i = np.round(y * 8388607).astype('<i4')
    w.writeframes(i.view(np.uint8).reshape(-1, 4)[:, :3].tobytes())
    w.close()


class Track:
    def __init__(self, seconds, seed):
        self.n = int(round(seconds * SR))
        self.y = np.zeros((self.n, 2))
        self.rs = np.random.RandomState(seed)

    def put(self, x, at, pan=0.0):
        x = np.asarray(x)
        i = int(round(at * SR)) % self.n
        idx = (i + np.arange(len(x))) % self.n  # wraps: the loop's tails fall into its start
        self.y[idx, 0] += x * (1 - pan) / 2
        self.y[idx, 1] += x * (1 + pan) / 2


def lowpass(x, hz):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return np.fft.irfft(X / (1 + (f / hz) ** 4), len(x))


def env(n, tau, attack=0.002):
    t = np.arange(n) / SR
    return np.minimum(1, t / attack) * np.exp(-t / tau)


def kick(v=1.0, length=0.3):
    t = np.arange(int(length * SR)) / SR
    return v * np.sin(2 * np.pi * np.cumsum(46 + 120 * np.exp(-t / 0.03)) / SR) * np.exp(-t / 0.12)


def rim(rs, v=1.0):
    t = np.arange(int(0.07 * SR)) / SR
    return v * 0.35 * (np.sin(2 * np.pi * 1750 * t) + 0.5 * np.sin(2 * np.pi * 430 * t)) * np.exp(-t / 0.012) + v * 0.1 * rs.randn(len(t)) * np.exp(-t / 0.006)


def hat(rs, v=1.0, tau=0.012):
    n = int(max(0.05, 5 * tau) * SR)
    return v * 0.12 * np.diff(rs.randn(n + 1)) * np.exp(-np.arange(n) / SR / tau)


def clap(rs, v=1.0):
    n = int(0.2 * SR); t = np.arange(n) / SR
    burst = sum(np.exp(-np.maximum(0, t - d) / 0.006) * (t >= d) for d in (0, 0.01, 0.02))
    return v * 0.3 * lowpass(rs.randn(n), 3000) * (burst + np.exp(-t / 0.08))


def chord(rs, notes, dur, v=1.0, bright=2500.0):
    n = int(dur * SR); t = np.arange(n) / SR; s = np.zeros(n)
    for m in notes:
        f = 440 * 2 ** ((m - 69) / 12)
        for det in (-7, 0, 6):
            s += 2 * ((f * 2 ** (det / 1200) * t + rs.rand()) % 1) - 1
    return v * 0.08 * lowpass(s, bright) * env(n, dur * 0.45, 0.004)


def bass(m, dur, v=1.0):
    n = int(dur * SR); t = np.arange(n) / SR; f = 440 * 2 ** ((m - 69) / 12)
    return v * 0.5 * np.tanh(1.5 * np.sin(2 * np.pi * f * t)) * env(n, dur * 0.6, 0.003)


def pad_piece(seconds, seed=3):
    tr = Track(seconds, seed); t = np.arange(tr.n) / SR; s = np.zeros(tr.n)
    for m in (50, 57, 60, 64, 67):
        f = 440 * 2 ** ((m - 69) / 12)
        s += 2 * ((f * t * (1 + 0.002 * np.sin(2 * np.pi * 0.3 * t)) + tr.rs.rand()) % 1) - 1
    s = lowpass(s, 900) * (0.6 + 0.4 * np.sin(2 * np.pi * t / seconds))
    tr.y[:, 0] += s; tr.y[:, 1] += np.roll(s, 300)
    return tr.y


Dm7 = [50, 57, 60, 65, 69]; Gm7 = [55, 58, 62, 65, 70]; Bb = [46, 58, 62, 65, 69]; A7 = [45, 57, 61, 64, 67]


def dubloop():
    bpm = 120; b = 60 / bpm; tr = Track(4 * b, 11)
    for k in range(4):
        tr.put(kick(0.9), k * b); tr.put(hat(tr.rs, 0.8), (k + 0.5) * b)
    tr.put(chord(tr.rs, Dm7, 0.35, 0.9), 0.5 * b, -0.2); tr.put(chord(tr.rs, Dm7, 0.3, 0.7), 2.75 * b, 0.2)
    return tr.y, bpm, 4


def drums():
    bpm = 120; b = 60 / bpm; tr = Track(32 * b, 12)
    for bar in range(8):
        o = bar * 4 * b
        if bar in (3, 7):
            tr.put(kick(), o); tr.put(rim(tr.rs, 0.9), o + b)
            continue
        for k in range(4):
            tr.put(kick(), o + k * b); tr.put(hat(tr.rs, tr.rs.uniform(0.7, 1)), o + (k + 0.5) * b)
        for k in (1, 3):
            tr.put(rim(tr.rs, 0.8), o + k * b)
    return tr.y, bpm, 4


def voice():
    bpm = 120; b = 60 / bpm; tr = Track(32 * b, 13)
    phr = [(0, 0, 62, 0.45, 'a'), (0, 0.5, 65, 0.45, 'i'), (0, 1.0, 69, 0.9, 'o'), (2, 0, 69, 0.45, 'e'), (2, 0.5, 72, 0.45, 'a'),
           (2, 1.0, 69, 0.45, 'i'), (2, 1.5, 65, 0.95, 'o'), (4, 0, 62, 1.9, 'a'), (6, 0, 65, 0.45, 'i'), (6, 0.5, 62, 0.95, 'o')]
    F = {'a': (730, 1090, 2440), 'i': (270, 2290, 3010), 'o': (570, 840, 2410), 'e': (530, 1840, 2480)}
    for bar, beat, m, d, v in phr:
        n = int(d * 4 * b * SR); t = np.arange(n) / SR; f0 = 440 * 2 ** ((m - 69) / 12)
        ph = np.cumsum(f0 * (1 + 0.008 * np.sin(2 * np.pi * 5.2 * t) * np.minimum(1, t / 0.2))) / SR
        src = sum(np.sin(2 * np.pi * h * ph) / h for h in range(1, 40))
        X = np.fft.rfft(src); fr = np.fft.rfftfreq(n, 1 / SR)
        H = sum(a / (1 + ((fr - c) / (c * 0.07)) ** 2) for c, a in zip(F[v], (1, 0.5, 0.3)))
        y = np.fft.irfft(X * H, n) * np.minimum(1, np.minimum(t / 0.02, (t[-1] - t) / 0.06))
        tr.put(y, (bar * 4 + beat * 4) * b)
    return tr.y, bpm, 4


def sparse():
    bpm = 96; b = 60 / bpm; tr = Track(16 * b, 14)
    tr.put(chord(tr.rs, Dm7, 1.2, 0.8, 900), 0.0); tr.put(kick(0.5), 8 * b); tr.put(hat(tr.rs, 0.4, 0.03), 14.5 * b)
    return tr.y, bpm, 4


def dense():
    bpm = 140; b = 60 / bpm; tr = Track(8 * b, 15)
    roots = [38, 38, 41, 36]
    for k in range(8):
        tr.put(kick(1.0, 0.25), k * b)
        tr.put(bass(roots[k // 2] + (12 if k % 2 else 0), b / 2 * 0.9, 0.6), (k + 0.5) * b)
        if k % 2:
            tr.put(clap(tr.rs, 0.9), k * b, 0.1)
        tr.put(hat(tr.rs, 0.6, 0.06), (k + 0.5) * b, 0.3)
    for i in range(32):
        tr.put(hat(tr.rs, 0.9 if i % 4 == 2 else 0.5), i * b / 4, -0.3)
    return tr.y, bpm, 4


def keys_and_groove(seconds_bars, bpm, rs, with_drums, with_keys):
    b = 60 / bpm; tr = Track(seconds_bars * 4 * b, rs)
    for bar in range(0, seconds_bars, 2):
        o = bar * 4 * b
        if with_keys:
            for at, ch, d, v in ((0.5, Dm7, 0.35, 0.9), (3.25, Gm7, 0.9, 0.75), (4.5, Bb, 0.35, 0.9), (7.25, A7, 0.9, 0.8)):
                tr.put(chord(tr.rs, ch, d * b * 2.2, v), o + at * b, tr.rs.uniform(-0.5, 0.5))
        if with_drums:
            for at, v in ((0, 1), (2, 0.9), (4, 1), (6, 0.9), (7.5, 0.5)):
                tr.put(kick(v * 0.8), o + at * b)
            for at in (1, 3, 5, 7):
                tr.put(rim(tr.rs, 0.8), o + at * b, 0.2)
            for i in range(16):
                tr.put(hat(tr.rs, 1.0 if i % 2 else 0.55), o + i * b / 2, -0.2)
    return tr.y


def sections():
    bpm = 127; b = 60 / bpm
    parts = [keys_and_groove(8, bpm, 21, True, False), keys_and_groove(8, bpm, 22, False, True),
             pad_piece(4 * 4 * b, 23), keys_and_groove(12, bpm, 24, True, True)]
    return np.concatenate(parts), bpm, 4


def waltz():
    bpm = 92; b = 60 / bpm; tr = Track(4 * 3 * b, 16)
    chords = [Dm7, Gm7, Bb, A7]
    for bar in range(4):
        o = bar * 3 * b
        tr.put(bass(chords[bar][0] - 12, b * 0.9, 0.8), o); tr.put(kick(0.6), o)
        for k in (1, 2):
            tr.put(chord(tr.rs, chords[bar], b * 0.6, 0.6, 1800), o + k * b, 0.3 * (k * 2 - 3))
        for k in range(6):
            tr.put(hat(tr.rs, 0.7 if k % 2 else 0.4), o + k * b / 2)
    return tr.y, bpm, 3


def sixeight():
    bpm = 120; e = 60 / bpm / 2; tr = Track(2 * 6 * e, 17)  # a bar is six 8ths = three quarter notes
    notes = [38, 45, 50, 45, 41, 45, 36, 43, 48, 43, 40, 43]
    for i in range(12):
        tr.put(bass(notes[i], e * 0.9, 0.5), i * e)
        tr.put(hat(tr.rs, 0.8 if i % 3 == 0 else 0.4), i * e)
    for bar in range(2):
        tr.put(kick(1.0), bar * 6 * e); tr.put(rim(tr.rs, 0.9), bar * 6 * e + 3 * e)
    return tr.y, bpm, 3


def fast():
    bpm = 174; b = 60 / bpm; tr = Track(8 * b, 18)
    for at in (0, 2.5, 4, 6.5, 7.25):
        tr.put(kick(1.0, 0.2), at * b)
    for at in (1, 3, 5, 7):
        tr.put(rim(tr.rs, 1.0), at * b); tr.put(clap(tr.rs, 0.5), at * b)
    for at in (3.75, 5.5):
        tr.put(rim(tr.rs, 0.4), at * b)
    for i in range(16):
        tr.put(hat(tr.rs, 0.5 + 0.3 * (i % 2)), i * b / 2, 0.3)
    tr.put(bass(31, 3.5 * b, 0.8), 0); tr.put(bass(34, 3.5 * b, 0.8), 4 * b)
    return tr.y, bpm, 4


def wav_seconds(path):
    w = wave.open(path, 'rb'); s = w.getnframes() / w.getframerate(); w.close(); return s


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    out = sys.argv[1]
    own = sys.argv[sys.argv.index('--own') + 1] if '--own' in sys.argv else None
    os.makedirs(out, exist_ok=True)
    pieces = []

    def add(name, path, bpm, bar):
        loop = wav_seconds(path)
        pieces.append(dict(name=name, file=os.path.basename(path), bpm=bpm, barBeats=bar, loopSeconds=round(loop, 4),
                           repeat=int(np.ceil(FIVE_MINUTES / loop)) + 1))

    for name in ('keys', 'groove'):
        dst = os.path.join(out, f'{name}.wav')
        shutil.copyfile(os.path.join(LOOPS, f'fly-{name}_127bpm.wav'), dst)
        add(name, dst, 127, 4)
    for name, make in (('dubloop', dubloop), ('drums', drums), ('voice', voice), ('sparse', sparse), ('dense', dense),
                       ('sections', sections), ('waltz', waltz), ('sixeight', sixeight), ('fast', fast)):
        y, bpm, bar = make()
        dst = os.path.join(out, f'{name}.wav')
        write(dst, y)
        add(name, dst, bpm, bar)
    dst = os.path.join(out, 'pad.wav')
    write(dst, pad_piece(16 * 0.5, 3))
    add('pad', dst, 120, 4)
    if own and os.path.isdir(own):
        for f in sorted(os.listdir(own)):
            m = re.search(r'_(\d+(?:\.\d+)?)bpm(?:_(3-4|6-8))?\.wav$', f, re.I)
            if not m:
                continue
            name = 'own-' + re.sub(r'[^a-z0-9]+', '-', f[:m.start()].lower()).strip('-')
            dst = os.path.join(out, name + '.wav')
            shutil.copyfile(os.path.join(own, f), dst)
            add(name, dst, float(m.group(1)), 3 if m.group(2) else 4)
    with open(os.path.join(out, 'pieces.json'), 'w') as f:
        json.dump(pieces, f, indent=1)
    print(f'{len(pieces)} pieces in {out}')


if __name__ == '__main__':
    main()
