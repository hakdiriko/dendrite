"""Writes Source/flies/<fly>/Reference.cpp: how hard each of the cut's neurons works on ordinary music.

The 2-DG film (3.6) shows what *this session* did to the fly, so it needs something to hold the session
against. That is the same fly, fresh, played six reference sounds (tools/render/material.py: a dub loop,
drums, chords, a pad, a voice, pink noise), each for two minutes through the offline renderer with
--fly-dump. A neuron's reference rate is its mean rate over all six, in Hz. The fly at rest (silence)
hardly fires at all (3 spikes in 120 s on the male), so rest is no baseline; ordinary music is.

usage: python3 tools/rig/reference.py malecns|flywire RENDER_BINARY
       (build the renderer for that fly first: see tools/render/render.cpp; flywire needs
        -DDENDRITE_FLY_FLYWIRE=1 and Source/flies/flywire/FlyData.cpp)
Needs numpy and soundfile."""
import json, os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
FLY, RENDER = sys.argv[1], sys.argv[2]
assert FLY in ('flywire', 'malecns')
SOUNDS = ['loop', 'drums', 'chords', 'pad', 'voice', 'pink']

with tempfile.TemporaryDirectory() as tmp:
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'render', 'material.py'), os.path.join(tmp, 'mat')], check=True, stdout=subprocess.DEVNULL)
    total, seconds = None, 0.0
    for s in SOUNDS:
        dump = os.path.join(tmp, 'dump-' + s)
        os.makedirs(dump)
        subprocess.run([RENDER, os.path.join(tmp, 'mat', s + '.wav'), os.path.join(tmp, s + '-out.wav'), '--repeat', '8', '--seed', '1', '--fly-dump', dump],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        meta = json.load(open(os.path.join(dump, 'meta.json')))
        spikes = np.load(os.path.join(dump, 'spikes.npy')).astype(np.int64)
        total = spikes.sum(axis=0) if total is None else total + spikes.sum(axis=0)
        seconds += spikes.shape[0] * meta['bin']
        print(s, spikes.shape[0] * meta['bin'], 's', int(spikes.sum()), 'spikes', file=sys.stderr)

hz = total / seconds
# kept as hundredths of a Hz in 16 bits (up to 655 Hz)
q = np.clip(np.round(hz * 100.0), 0, 65535).astype(np.int64)
lines = []
for k in range(0, len(q), 24):
    lines.append('    ' + ', '.join(str(int(v)) for v in q[k:k + 24]) + ',')
out = os.path.join(ROOT, 'Source', 'flies', FLY, 'Reference.cpp')
with open(out, 'w') as f:
    f.write('// How hard each of the cut\'s neurons works on ordinary music (generated: tools/rig/reference.py).\n')
    f.write('// Indexed like FlyData; mean rate over six reference sounds (' + ', '.join(SOUNDS) + '), %.0f s in all,\n' % seconds)
    f.write('// in hundredths of a Hz.\n#include "Reference.h"\n\nnamespace dendrite::flydata\n{\nconst uint16_t referenceCentiHz[neurons] = {\n')
    f.write('\n'.join(lines) + '\n};\n} // namespace dendrite::flydata\n')
print('wrote', out, 'mean %.2f Hz, %d of %d neurons above 0.1 Hz' % (hz.mean(), int((hz > 0.1).sum()), len(hz)), file=sys.stderr)
