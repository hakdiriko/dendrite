# The fly, Phase 3: the memory gets a sense of place

Measured 2026-11-17 on top of 2.5.0 (fear that falls, the real turn), with the Phase 0 instruments (`tools/fly/probe/`) on the same twelve pieces. Every comparison is against 2.5.0 itself, measured the same day (`tools/fly/probe/main-2.5.0-2026-11-17.json`). The first build, on 2.4.3 (2026-11-16), gave the same gate numbers: bar 81%, loop 63%.

## Gate: passed, narrowly

The plan's gate: from the memory cells alone, the place in the bar is read right at least 80% of the time and the 16th in the loop at least 60%, with under 10% of the cells active at once. It was fixed before measuring as means over all twelve pieces, read from one 50 ms bin at a time. That is the same instrument as the baseline, and the stricter one.

| | gate | 2.5.0 | Phase 3 |
|---|---|---|---|
| Place in the bar, from Kenyon cells | 80% | (not carried) | **81%** (76 to 88% per piece) |
| 16th in the loop, from Kenyon cells | 60% | 20% | **64%** (75 to 84% on loops of 1 or 2 bars; 28% and 52% on the 8-bar loops; 15% on the 32-bar passage) |
| Kenyon cells active in a 50 ms bin | under 10% | up to 3.1% | **1.3% at most** |

**What passing means and what it does not.**
- **The place is there because we feed it in.** With the wiring scrambled, the Kenyon cells read the bar just as well (81% against 81%). A random expansion of a clock gives a readable code, and that is what a mushroom body is. So the memory can now hold "when", but this does not show that the fly's wiring adds anything.
- **Only the mean passes.** Four pieces read the bar below 80% (drums and voice 76%, pad 78%, the dub loop 79%). The 16th in a long loop is far below 60%.
- **The outputs know the place too.** The output neurons read the 16th in the loop 46% of the time (2.5.0: 24%), because the visual projection neurons reach more than the Kenyon cells.

## What changed

- **The cut** (`tools/fly/cut3.py`) is built by weighted reach, instead of by what fired on one loop:
  - A signal flows forward from the senses and back from the descending neurons, four steps each. A neuron's reach is how much passes through it, and the 1,500 reflex neurons with the most reach are kept.
  - Also kept: every sense; the whole mushroom body (every Kenyon cell, MBON and DAN, APL and DPM); and the escape path, DNg11 and the steering neurons DNa01 and DNa02. Each of those comes with every input of at least 10 synapses and the five strongest inputs of each of those.
  - Weighted reach alone dropped the grooming circuit, and DNg11 went silent. Plain reach, every neuron within 2 or 3 strong steps, is 27,000 to 110,000 neurons.
  - The result is 12,139 neurons and 1,000,741 connections, against 11,705 and 1,034,261 before. That includes 5,177 Kenyon cells (2,459 before) and 62,261 learning synapses (28,761 before).
- **The place inputs:** the 73 visual projection neurons that make at least 20 synapses onto Kenyon cells (aMe12, aMe26, MTe30/32/40, LTe25 and others). In the real fly these bring sight to the mushroom body's accessory calyx. They reach 420 Kenyon cells, mostly gamma-d and alpha/beta-p. Each one carries one thing and is tuned to one value of it, firing up to 300 spikes per second:
  - the 16th in the bar: 29 neurons, each as wide as two 16ths;
  - the 4-bar phrase: 15; the 8-bar phrase: 11;
  - the last move: 11; the time since it: 7.
- **The phrase** starts at the host's bar count. It starts again at a bar whose smell differs from the four bars before it by more than 35%, but not within two bars of the last start.
- **Less noise.** At 150 spikes per second the cells read the bar 69 to 71% of the time; at 300 they read it 83%. About 1% of the cells are active either way.

## The rest of the fly, against 2.5.0

- **Moves:** about the same, 35 against 38 in five minutes.
- **Fear** still falls as 2.5.0 made it: 0.33 at five minutes, against 0.25.
- **The scratch is much rarer**, so this needs a listen before it ships. Keys 1%, against 26%; groove 11%, against 29%; waltz 28%, against 37%. The grooming circuit is now a different set of neurons. Hakan asked for fewer moves, but this was not a choice.
- **The brain matters more for timing than it did:**
  - The stand-in now matches the real fly's moments 16% of the time. A second real fly matches 32%. In 2.5.0 those were 25% and 36%.
  - The scrambled fly hardly moves: 2 moves, against 11 in 2.5.0.
- **Cost on an idle machine:**

  | piece | 2.5.0 | Phase 3 |
  |---|---|---|
  | keys | 7.5% of a core | 9.0% |
  | groove | 7.7% | 9.5% |
  | sparse | 6.4% | 7.8% |

  The 1-in-1000 block is 15 to 19% in 2.5.0 and 20 to 23% here. The bigger mushroom body doubles the learning synapses, and their bookkeeping runs every 10 ms. Phase 3 costs about a fifth more than 2.5.0.
- **Saved sets:** a fly memory saved by 2.x does not fit the new brain, so such a set opens with a fresh fly (the size check rejects the old memory). The saved memory is now about 125 KB.

## The full report (Phase 3 on 2.5.0)

#### The senses alone explain the outputs (real fly, R² on the second half; senses over the last 300 ms)

| piece | escape | excited | groom | lean | escape, scrambled |
|---|---|---|---|---|---|
| keys | 99% | 98% | 94% | 40% | 94% |
| groove | 94% | 77% | 80% | 8% | 73% |
| dubloop | 95% | 65% | 51% | 19% | 76% |
| drums | 97% | 99% | 90% | 49% | 86% |
| voice | 98% | 97% | 90% | 58% | 89% |
| sparse | 99% | 93% | 98% | 45% | 98% |
| dense | 92% | 58% | 17% | -18% | 73% |
| sections | 94% | 89% | 93% | 8% | 86% |
| waltz | 98% | 90% | 94% | 13% | 86% |
| sixeight | 95% | 89% | 79% | 7% | 76% |
| fast | 95% | 89% | 75% | -8% | 80% |
| pad | 76% | 96% | 90% | 63% | 67% |
| **mean** | 94% | 87% | 79% | 24% | 82% |

#### Place in the loop, read from one 50 ms bin (real fly, second half)

| piece | 16ths | chance | raw senses | output neurons | Kenyon cells | MBONs | KC Hz | KCs active per bin |
|---|---|---|---|---|---|---|---|---|
| keys | 32 | 3% | 65% | 61% | 81% | 25% | 0.21 | 1% |
| groove | 32 | 3% | 93% | 61% | 78% | 25% | 0.31 | 1% |
| dubloop | 16 | 6% | 80% | 69% | 79% | 42% | 0.23 | 1% |
| drums | 128 | 1% | 36% | 20% | 28% | 7% | 0.24 | 1% |
| voice | 128 | 1% | 28% | 38% | 52% | 15% | 0.30 | 1% |
| sparse | 64 | 2% | 20% | 40% | 49% | 15% | 0.23 | 1% |
| dense | 32 | 3% | 90% | 53% | 75% | 21% | 0.19 | 1% |
| sections | 512 | 0% | 31% | 12% | 15% | 3% | 0.29 | 1% |
| waltz | 48 | 2% | 70% | 54% | 84% | 22% | 0.31 | 1% |
| sixeight | 24 | 4% | 73% | 50% | 75% | 22% | 0.18 | 1% |
| fast | 32 | 3% | 87% | 62% | 75% | 23% | 0.20 | 1% |
| pad | 64 | 2% | 80% | 35% | 71% | 7% | 0.18 | 1% |
| **mean** | | 2% | 63% | 46% | 64% | 19% | 0.24 | |

#### Place in the bar, read from one 50 ms bin (real fly, second half)

| piece | 16ths | chance | raw senses | output neurons | Kenyon cells | MBONs | Kenyon cells, scrambled |
|---|---|---|---|---|---|---|---|
| keys | 16 | 6% | 66% | 72% | 84% | 43% | 82% |
| groove | 16 | 6% | 91% | 80% | 84% | 42% | 86% |
| dubloop | 16 | 6% | 80% | 69% | 79% | 42% | 79% |
| drums | 16 | 6% | 38% | 59% | 76% | 36% | 75% |
| voice | 16 | 6% | 30% | 48% | 76% | 25% | 73% |
| sparse | 16 | 6% | 26% | 60% | 84% | 27% | 80% |
| dense | 16 | 6% | 87% | 71% | 82% | 37% | 86% |
| sections | 16 | 6% | 46% | 61% | 82% | 31% | 81% |
| waltz | 12 | 8% | 85% | 83% | 88% | 51% | 89% |
| sixeight | 12 | 8% | 73% | 69% | 81% | 37% | 80% |
| fast | 16 | 6% | 88% | 75% | 83% | 39% | 86% |
| pad | 16 | 6% | 66% | 48% | 78% | 18% | 76% |
| **mean** | | 7% | 65% | 66% | 81% | 36% | 81% |

#### Four flies, five minutes each (r real, s scrambled, st stand-in, r2 real with other chance)

| piece | moves r / s / st / r2 | per bar (r) | fear at 5 min r / s / st / r2 | scratching r / s / st / r2 | spots alike r~s / r~st / r~r2 | same moments r~s / r~st / r~r2 | longest same move (r) |
|---|---|---|---|---|---|---|---|
| keys | 31 / 4 / 26 / 30 | 0.19 | 0.33 / 0.13 / 0.00 / 0.30 | 1% / 0% / 0% / 2% | 25% / 72% / 72% | 3% / 16% / 52% | 2 |
| groove | 44 / 1 / 24 / 40 | 0.27 | 0.40 / 0.16 / 0.00 / 0.40 | 11% / 0% / 9% / 10% | 36% / 63% / 79% | 2% / 9% / 23% | 2 |
| dubloop | 31 / 3 / 23 / 45 | 0.20 | 0.32 / 0.05 / 0.00 / 0.33 | 4% / 0% / 1% / 5% | 37% / 8% / 83% | 3% / 3% / 29% | 2 |
| drums | 49 / 1 / 45 / 65 | 0.31 | 0.45 / 0.13 / 0.00 / 0.44 | 14% / 0% / 11% / 14% | 24% / 86% / 76% | 2% / 51% / 55% | 2 |
| voice | 25 / 4 / 25 / 27 | 0.16 | 0.28 / 0.01 / 0.00 / 0.28 | 2% / 0% / 1% / 1% | 16% / 88% / 81% | 4% / 36% / 48% | 2 |
| sparse | 20 / 10 / 21 / 21 | 0.16 | 0.27 / 0.00 / 0.00 / 0.27 | 4% / 0% / 2% / 4% | 55% / 89% / 82% | 5% / 5% / 5% | 2 |
| dense | 24 / 1 / 25 / 37 | 0.14 | 0.31 / 0.10 / 0.00 / 0.33 | 4% / 0% / 1% / 4% | 21% / 12% / 77% | 4% / 4% / 38% | 2 |
| sections | 64 / 0 / 32 / 69 | 0.33 | 0.33 / 0.06 / 0.00 / 0.34 | 4% / 0% / 2% / 4% | 0% / 34% / 55% | 0% / 16% / 38% | 2 |
| waltz | 63 / 0 / 45 / 55 | 0.39 | 0.40 / 0.06 / 0.00 / 0.39 | 28% / 0% / 27% / 28% | 0% / 84% / 92% | 0% / 24% / 40% | 2 |
| sixeight | 30 / 1 / 16 / 29 | 0.15 | 0.38 / 0.13 / 0.00 / 0.34 | 12% / 0% / 16% / 12% | 33% / 19% / 77% | 3% / 3% / 17% | 2 |
| fast | 38 / 1 / 16 / 51 | 0.17 | 0.29 / 0.13 / 0.00 / 0.32 | 9% / 0% / 4% / 9% | 18% / 17% / 83% | 3% / 3% / 18% | 2 |
| pad | 5 / 1 / 1 / 7 | 0.03 | 0.16 / 0.01 / 0.00 / 0.17 | 1% / 0% / 1% / 1% | 20% / 20% / 43% | 20% / 20% / 20% | 2 |
| **mean** | 35.3 / 2.2 / 24.9 / 39.7 | | 0.33 / 0.08 / 0.00 / 0.33 | | 24% / 50% / 75% | 4% / 16% / 32% | |

#### Move kinds

| piece | real | scrambled | stand-in | real, other chance |
|---|---|---|---|---|
| keys | leap 1, hold 16, sink 14 | leap 1, hold 2, sink 1 | leap 1, hold 12, sink 13 | leap 1, hold 17, sink 12 |
| groove | hop 5, leap 17, duck 16, rush 6 | leap 1 | hop 8, leap 11, duck 5 | hop 3, leap 16, duck 16, rush 5 |
| dubloop | hop 1, leap 16, duck 14 | hop 2, leap 1 | hop 15, leap 1, duck 7 | hop 6, leap 22, duck 12, rush 5 |
| drums | hop 2, leap 17, duck 18, rush 12 | leap 1 | hop 2, leap 28, duck 12, rush 3 | hop 10, leap 16, duck 22, rush 17 |
| voice | leap 1, hold 12, sink 12 | leap 1, hold 1, sink 2 | leap 1, hold 12, sink 12 | leap 1, hold 13, sink 13 |
| sparse | leap 1, hold 9, sink 10 | leap 1, hold 6, sink 3 | leap 1, hold 11, sink 9 | leap 1, hold 10, sink 10 |
| dense | hop 2, leap 12, duck 8, rush 2 | leap 1 | hop 16, leap 2, duck 7 | hop 4, leap 14, duck 14, rush 5 |
| sections | hop 5, leap 20, duck 13, rush 15, hold 5, sink 6 | – | hop 7, leap 12, duck 4, hold 3, sink 6 | hop 6, leap 19, duck 24, rush 10, hold 5, sink 5 |
| waltz | hop 4, leap 20, duck 21, rush 18 | – | hop 8, leap 23, duck 9, rush 5 | hop 4, leap 16, duck 17, rush 18 |
| sixeight | hop 9, leap 11, duck 10 | leap 1 | hop 10, leap 1, duck 5 | hop 2, leap 15, duck 11, rush 1 |
| fast | hop 3, leap 19, duck 11, rush 5 | leap 1 | hop 10, leap 1, duck 5 | leap 23, duck 14, rush 14 |
| pad | leap 1, hold 2, sink 2 | leap 1 | leap 1 | leap 1, hold 2, sink 4 |

#### Cost (render machine, not the plug-in: one run, timing is not reproducible)

| piece | mean load real | 1 block in 1000 real | slowest block real | mean load stand-in |
|---|---|---|---|---|
| keys | 10% | 26% | 146% | 6% |
| groove | 11% | 95% | 220% | 6% |
| dubloop | 11% | 97% | 277% | 6% |
| drums | 14% | 112% | 386% | 5% |
| voice | 12% | 101% | 297% | 5% |
| sparse | 8% | 93% | 267% | 5% |
| dense | 12% | 101% | 252% | 5% |
| sections | 13% | 110% | 394% | 5% |
| waltz | 15% | 170% | 306% | 5% |
| sixeight | 15% | 112% | 401% | 5% |
| fast | 12% | 102% | 386% | 6% |
| pad | 12% | 101% | 203% | 6% |

The stand-in's own fit (pooled, last quarter of each render held out): escape 92%, groom 90%, lean 49%, excited 90%.

