"""Does the fly's fear fall? Phase 1 of the plan (relief learning): after five minutes on one piece
fear should sit between 0.2 and 0.6, and it should rise again when the music changes.
Renders each loop for 5 minutes, and a change piece (4 minutes of one, then 2 of the other),
with the offline renderer, and reads fear out of the fly log.
usage: python3 tools/fly/fear.py [--render build/render] [--out dir] [--taste spec]"""
import argparse, os, subprocess, wave, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); LOOPS = os.path.join(HERE, 'taste', 'loops')
ap = argparse.ArgumentParser(); ap.add_argument('--render', default='build/render')
ap.add_argument('--out', default='/tmp/fly-fear'); ap.add_argument('--taste', default='')
ap.add_argument('--bpm', default='127'); a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)

def read(p):
    with wave.open(p) as w:
        b, n, c = w.getsampwidth(), w.getnframes(), w.getnchannels(); raw = w.readframes(n)
        if b == 3: raw = np.frombuffer(raw, np.uint8).reshape(-1, 3); x = (raw[:, 1].astype(np.int16) | (raw[:, 2].astype(np.int16) << 8))
        else: x = np.frombuffer(raw, np.int16)
        return x.reshape(-1, c), w.getframerate()
def write(p, x, sr):
    with wave.open(p, 'wb') as w: w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(sr); w.writeframes(x.tobytes())
def tile(x, sr, sec): return np.tile(x, (int(np.ceil(sec * sr / len(x))), 1))[:int(sec * sr)]

g, sr = read(os.path.join(LOOPS, 'fly-groove_127bpm.wav')); k, _ = read(os.path.join(LOOPS, 'fly-keys_127bpm.wav'))
pieces = {'groove': tile(g, sr, 300), 'keys': tile(k, sr, 300),
          'groove-then-keys': np.concatenate([tile(g, sr, 240), tile(k, sr, 120)]),
          'keys-then-groove': np.concatenate([tile(k, sr, 240), tile(g, sr, 120)])}
def fear(name, x):
    src, log = os.path.join(a.out, name + '.wav'), os.path.join(a.out, name + '.csv')
    write(src, x, sr)
    cmd = [a.render, src, os.path.join(a.out, name + '-out.wav'), '--bpm', a.bpm, '--fly-log', log, '--tail', '0']
    if a.taste: cmd += ['--taste', a.taste]
    subprocess.run(cmd, check=True, capture_output=True)
    d = np.loadtxt(log, delimiter=','); return d[:, 0], d[:, 12], d[:, 1]
from concurrent.futures import ThreadPoolExecutor
with ThreadPoolExecutor(4) as pool: runs = dict(zip(pieces, pool.map(lambda kv: fear(*kv), pieces.items())))
ok = True
for name in pieces:
    t, f, jump = runs[name]
    at = lambda s: float(f[np.abs(t - s).argmin()])
    jumps = int(((jump[1:] > 0.5) & (jump[:-1] <= 0.5)).sum())
    line = f'{name:18s} fear at 1, 2, 3, 4, 5 min: ' + ' '.join(f'{at(60 * m):.2f}' for m in range(1, 6))
    if 'then' in name:
        after = float(f[(t > 240) & (t < 300)].max())
        rise = after > at(239) + 0.05
        line += f'  | peak in the minute after the change {after:.2f} ({"rises" if rise else "does not rise"})'
        ok &= rise
    else:
        inside = 0.2 <= at(299) <= 0.6; ok &= inside
        line += f'  ({"inside" if inside else "OUTSIDE"} 0.2..0.6)'
    print(line + f', max {f.max():.2f}, {jumps} jumps')
print('PASS' if ok else 'FAIL')
