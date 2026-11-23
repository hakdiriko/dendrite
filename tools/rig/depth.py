"""Writes Source/flies/<fly>/Depth.cpp: how deep each of the cut's neurons sits, front to back.

FlyData keeps each neuron's place seen from the front (x, y). The goniometer turns the brain, so the
window needs the third coordinate too. It is taken from the same atlas the same way:
  flywire  the annotation's pos_z (40 nm sections), matched by root id (Source/flies/flywire/Identities.cpp)
  malecns  the soma's z (8 nm voxels); for a neuron whose soma lies outside the volume (the ear's and the
           eye's sense neurons) the mean z of its synapses, as tools/fly/malecns.py places x and y
Before writing, x and y are rebuilt the same way and must land within one step of FlyData's
neuronX/neuronY for every neuron, so z is proven to belong to the right neuron.

usage: python3 tools/rig/depth.py flywire DATA_DIR    (DATA_DIR as for tools/rig/identities.py)
       python3 tools/rig/depth.py malecns             (tools/fly/data/malecns, as tools/fly/malecns.py)
Needs numpy, pandas, pyarrow."""
import os, re, sys
import numpy as np, pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
FLY = sys.argv[1]
assert FLY in ('flywire', 'malecns')
FD = os.path.join(ROOT, 'Source', 'flies', FLY)


def carray(src, name):
    m = re.search(r'\b' + name + r'\[[^\]]*\] = \{(.*?)\};', src, re.S)
    return np.array([int(v) for v in re.findall(r'-?\d+', m.group(1))], dtype=np.int64)


data = open(os.path.join(FD, 'FlyData.cpp')).read()
X, Y = carray(data, 'neuronX'), carray(data, 'neuronY')
ids = carray(open(os.path.join(FD, 'Identities.cpp')).read(), 'rootId')
N = len(ids)
assert len(X) == N

if FLY == 'flywire':
    D = sys.argv[2].rstrip('/') + '/'
    ann = pd.read_csv(D + 'Supplemental_file1_neuron_annotations.tsv', sep='\t', low_memory=False).set_index('root_id').reindex(ids)
    x, y, z = ann.pos_x.values, ann.pos_y.values, ann.pos_z.values
    x0, x1, y0, y1 = 22000.0, 226000.0, 12800.0, 111000.0
    ZNM = 40.0
    x, y = np.nan_to_num(x, nan=124000), np.nan_to_num(y, nan=60000)  # the export's place for a neuron with none
else:
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'fly'))
    import malecns
    a, _ = malecns.annotations()
    loc = np.array([v if v is not None and len(v) == 3 else [np.nan] * 3 for v in a.somaLocation.reindex(ids).values], float)
    x, y, z = loc[:, 0].copy(), loc[:, 1].copy(), loc[:, 2].copy()
    need = np.isnan(x)
    import pyarrow.ipc as ipc
    ix = pd.Index(ids)
    s = np.zeros((3, N)); sn = np.zeros(N)
    r = ipc.open_file(malecns.D + 'syn-partners.feather')
    for b in range(r.num_record_batches):
        t = r.get_batch(b)
        for side in ('pre', 'post'):
            k = ix.get_indexer(t.column('body_' + side).to_numpy())
            ok = k >= 0
            k = k[ok]; keep = need[k]; k = k[keep]
            for c, axis in enumerate('xyz'):
                s[c] += np.bincount(k, t.column(axis + '_' + side).to_numpy()[ok][keep], N)
            sn += np.bincount(k, None, N)
    has = need & (sn > 0)
    for c, v in enumerate((x, y, z)):
        v[has] = s[c][has] / sn[has]
    print('depth: soma', int((~need).sum()), 'from synapses', int(has.sum()), 'none', int((need & ~has).sum()))
    # mirrored, as the export does, so the fly faces the way the FlyWire fly does; the export's box (over
    # the whole brain's places) is recovered from the neurons with a soma: the line that maps their
    # x and y onto FlyData's steps
    x = -x
    soma = ~need
    fx = np.polyfit(x[soma], X[soma], 1); fy = np.polyfit(y[soma], Y[soma], 1)
    x0, x1 = -fx[1] / fx[0], (65535 - fx[1]) / fx[0]
    y0, y1 = -fy[1] / fy[0], (65535 - fy[1]) / fy[0]
    ZNM = 8.0

# the proof: the same place, the same neuron
qx = np.rint(np.clip((np.nan_to_num(x, nan=(x0 + x1) / 2) - x0) / (x1 - x0), 0, 1) * 65535)
qy = np.rint(np.clip((np.nan_to_num(y, nan=(y0 + y1) / 2) - y0) / (y1 - y0), 0, 1) * 65535)
off = np.maximum(np.abs(qx - X), np.abs(qy - Y))
print('place check: worst', int(off.max()), 'steps; off by more than 1:', int((off > 1).sum()))
if (off > 1).any():
    sys.exit('places do not match FlyData; nothing written')

# the depth: nan (no place at all) sits at the middle; the box is the cut's own, 2% margin
zu = z * ZNM / 1000.0
zu = np.where(np.isnan(zu), np.nanmedian(zu), zu)
z0, z1 = np.percentile(zu, [0.05, 99.95]); m = 0.02 * (z1 - z0); z0, z1 = z0 - m, z1 + m
q = np.rint(np.clip((zu - z0) / (z1 - z0), 0, 1) * 65535).astype(int)
print('depth box um', round(z1 - z0, 1))

lines = ['// How deep each of the cut\'s neurons sits, front to back (generated: tools/rig/depth.py).',
         '// Indexed like FlyData; 0..65535 across depthMicrons, 0 at the front.',
         '#include "Depth.h"', '', 'namespace dendrite::flydata', '{',
         'const uint16_t neuronZ[neurons] = {']
for k in range(0, N, 24):
    lines.append('    ' + ', '.join(str(v) for v in q[k:k + 24]) + ',')
lines += ['};', '} // namespace dendrite::flydata', '']
open(os.path.join(FD, 'Depth.cpp'), 'w').write('\n'.join(lines))
open(os.path.join(FD, 'Depth.h'), 'w').write(
    '// How deep each neuron sits (generated: tools/rig/depth.py). See Source/rig/Depth.h.\n#pragma once\n\n'
    '#include "FlyData.h"\n\nnamespace dendrite::flydata\n{\n'
    f'constexpr float depthMicrons = {z1 - z0:.1f}f; // the cut\'s box, front to back\n'
    'extern const uint16_t neuronZ[neurons];         // 0..65535 across depthMicrons, 0 at the front\n'
    '} // namespace dendrite::flydata\n')
print('wrote', FD + '/Depth.cpp')
