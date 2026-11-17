"""The 3.0 cut (Phase 3 of the fly plan): the brain by reach instead of by what fired on one loop, and
with a sense of place reaching the memory.

Plain reach (every neuron a sense reaches within a few strong steps and that reaches an output) is
most of the brain: 27,000 to 110,000 neurons for 2 to 3 steps of at least 5 or 10 synapses. So reach
is weighed: a signal starts at the senses and flows forward four steps, each step split by how much
of a neuron's input comes from where (F); another flows back from the descending neurons four
steps, split by how much of a neuron's output goes where (B). A neuron's reach is F x B: how much
of what the fly senses passes through it on the way to what it does. The REFLEX (1,500) neurons with the
most reach are kept, beside:
  - every sense: the ears (Johnston's organ), the looming eyes (LPLC2, LC4), smell (the antennal
    lobe's projection neurons) and the place inputs below;
  - the whole mushroom body: every Kenyon cell, MBON and DAN, APL and DPM (the memory is the plan);
  - the escape path (DNp01, 02, 04, 11), DNg11 and the steering neurons (DNa01, DNa02), with every neuron that feeds them with at least
    10 synapses, and the five strongest inputs (at least 10 synapses) of each of those.
The antennal lobe's local circuit stays out, as in 2.2 (in this model it runs away).

The place inputs: the visual projection neurons that make at least 20 synapses onto Kenyon cells
(aMe12, aMe26, MTe*, LTe* and the like; in the real fly they bring sight to the mushroom body's
accessory calyx, mostly to the gamma-d and alpha/beta-p Kenyon cells). Since 3.0 they carry where
the fly is in the bar and the phrase, and what it last did.

usage (in tools/fly):  REFLEX=1500 python3 cut3.py keep3.npy place3.json
"""
import json, os, sys, collections
import numpy as np, scipy.sparse as sp
sys.argv, args = ['x'], sys.argv[1:]
import sim

REFLEX = int(os.environ.get('REFLEX', 1500))
STEPS = 4
PLACE_MIN = 20

ids, W, ann = sim.load()
g = sim.groups(ann)
cc = ann.cell_class.fillna('').values; ct = ann.cell_type.fillna('').values; sc = ann.super_class.fillna('').values
side = ann.side.fillna('').values
n = len(ids)
banned = np.isin(cc, ['olfactory', 'ALLN', 'LHLN', 'ALON'])
D = sp.diags((~banned).astype(np.float32))
C = (D @ (abs(W) / sim.WSYN) @ D).tocsr()

kc = np.where(cc == 'Kenyon_Cell')[0]
memory = np.where(np.isin(cc, ['Kenyon_Cell', 'MBON', 'DAN']) | np.isin(ct, ['APL', 'DPM']))[0]
alpn = np.array([k for k, c in enumerate(cc) if c == 'ALPN' and ct[k]])
kcSyn = np.asarray(C[:, kc].sum(1)).ravel()
place = np.where((sc == 'visual_projection') & (kcSyn >= PLACE_MIN))[0]
senses = np.unique(np.concatenate([np.concatenate(list(g.values())), alpn, place]))
dn = np.where(sc == 'descending')[0]
reflex = np.where(np.isin(ct, ['DNp01', 'DNp02', 'DNp04', 'DNp11', 'DNg11', 'DNa01', 'DNa02']))[0]

inS = np.asarray(C.sum(0)).ravel(); outS = np.asarray(C.sum(1)).ravel()
Fin = C @ sp.diags(1 / np.maximum(inS, 1))   # each connection as a share of the target's input
Fout = sp.diags(1 / np.maximum(outS, 1)) @ C  # and of the source's output
f = np.zeros(n); f[senses] = 1; F = f.copy()
b = np.zeros(n); b[dn] = 1; B = b.copy()
for _ in range(STEPS):
    f = Fin.T @ f; F += f
    b = Fout @ b; B += b
score = F * B

keep = np.zeros(n, bool)
keep[np.concatenate([senses, memory, reflex])] = True
# the reflexes' own circuits: the neurons that feed the escape path and DNg11 with at least SUPPORT
# synapses, and the ones that feed those, as long as a sense reaches them (by weighted reach alone the
# grooming circuit's inputs, which also talk to much else, fall out and DNg11 goes silent)
SUPPORT, TOP = 10, 5
Cc = C.tocsc()
front = reflex
for level in range(2):
    pre = []
    for t in front:
        col = Cc[:, t].toarray().ravel()
        col[banned | (F <= 0)] = 0
        best = np.argsort(-col)[:TOP if level else len(col)]
        pre += [p for p in best if col[p] >= SUPPORT]
    pre = np.unique(np.array(pre, int))
    pre = pre[~keep[pre]]
    keep[pre] = True
    front = pre
print('reflex support', int(keep.sum()) - len(np.unique(np.concatenate([senses, memory, reflex]))))
order = [i for i in np.argsort(-score) if not keep[i] and not banned[i] and score[i] > 0][:REFLEX]
keep[order] = True
keep &= ~banned
keep = np.where(keep)[0]
assert len(keep) < 65536
np.save(args[0], keep)
print(f'REFLEX {REFLEX}: neurons {len(keep)}, connections {C[keep][:, keep].nnz}, KCs {np.isin(kc, keep).sum()}, '
      f'DNs {np.isin(dn, keep).sum()}, place inputs {len(place)}')
print('by class', collections.Counter(cc[keep]).most_common(10))
json.dump(dict(place=[int(p) for p in place], placeTypes=[ct[p] for p in place], placeSides=[side[p] for p in place],
               placeKcSynapses=[float(kcSyn[p]) for p in place], reflex=REFLEX, steps=STEPS),
          open(args[1], 'w'), indent=1)
