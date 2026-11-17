#!/usr/bin/env python3
"""The fly's goal, measured on a fly log (build/render --fly-log): python3 tools/fly/goal.py fly.csv <bpm> [stats.json]

  on the grid    share of move starts, throw starts and scratch changes within 10 ms of a 16th line (goal: 95%)
  variety        how many kinds of move in the render, and the commonest one's share (goal: 3 or more, under 60%)
  not a loop     how alike one 4-bar phrase's moves are to the next (goal: under 60% alike)
  breathing      share of 16ths with nothing happening at all (goal: 30% to 80%: it must leave room)
  per bar        moves per bar (pass the bar's quarter notes with --bar 3 for 3/4 and 6/8)
  fear at 5 min  fear in the log at 300 s (None if the render is shorter), and at the end
  same in a row  the longest run of one kind of move, back to back
  slowest block  with build/render --stats: the slowest audio block's load (1.0 is a whole core), one in a thousand, and the mean
"""
import json
import sys
import numpy as np

NAMES = ['none', 'hop', 'leap', 'duck', 'dive', 'rush', 'hold', 'sink']


def measure(path, bpm, stats=None, bar=4.0):
    d = np.loadtxt(path, delimiter=',', ndmin=2)
    t, jump, thr, scratch = d[:, 0], d[:, 1], d[:, 3], d[:, 5]
    move = d[:, 13].astype(int) if d.shape[1] > 13 else (jump > 0).astype(int)
    six = 60.0 / bpm / 4.0
    hop = t[1] - t[0]

    def starts(x):
        return t[1:][(x[1:] > 0) & (x[:-1] <= 0)]

    def off(times):  # distance to the nearest 16th line, the log's own step taken off
        return np.maximum(0.0, np.abs((times + six / 2) % six - six / 2) - hop)

    events = np.concatenate([starts(move), starts(thr), starts(scratch - 0.1)])
    on_grid = float(np.mean(off(events) < 0.010)) if len(events) else 1.0
    kinds = move[1:][(move[1:] > 0) & (move[:-1] == 0)]
    counts = {NAMES[k] if k < len(NAMES) else str(k): int(np.sum(kinds == k)) for k in sorted(set(kinds))}
    top = max(counts.values()) / max(1, len(kinds)) if counts else 1.0
    # phrases: a 64-step picture of what happens in each 4 bars (which move, scratching or not)
    n16 = int(t[-1] / six)
    idx = np.minimum(len(t) - 1, ((np.arange(n16) + 0.5) * six / hop).astype(int))
    picture = move[idx] * 2 + (scratch[idx] > 0.1)
    phrases = [picture[i:i + 64] for i in range(0, n16 - 63, 64)]
    alike = float(np.mean([np.mean(a == b) for a, b in zip(phrases[2:], phrases[3:])])) if len(phrases) > 3 else float('nan')
    idle = float(np.mean(picture == 0))
    fear = d[:, 12]
    fear5 = round(float(fear[np.searchsorted(t, 300.0)]), 3) if t[-1] >= 300.0 else None
    run = best = 0
    for i, k in enumerate(kinds):
        run = run + 1 if i and k == kinds[i - 1] else 1
        best = max(best, run)
    out = dict(moves=len(kinds), per_bar=float(round(len(kinds) / max(1e-9, t[-1] / (six * 4 * bar)), 3)), on_grid=round(on_grid, 3),
               kinds=counts, commonest=round(top, 2), phrase_alike=round(alike, 2), idle=round(idle, 2),
               fear_5min=fear5, fear_end=round(float(fear[-1]), 3), same_in_a_row=best,
               scratching=round(float(np.mean(scratch > 0.1)), 3))
    if stats:
        with open(stats) as f:
            s = json.load(f)
        out.update(slowest_block=round(s['slowestLoadAfter1s'], 3), block_999=round(s.get('load999', float('nan')), 3), mean_load=round(s['meanLoad'], 3))
    return out


if __name__ == '__main__':
    args = [a for a in sys.argv[1:]]
    bar = 4.0
    if '--bar' in args:
        i = args.index('--bar'); bar = float(args[i + 1]); del args[i:i + 2]
    print(measure(args[0], float(args[1]) if len(args) > 1 else 120.0, args[2] if len(args) > 2 else None, bar))
