# Greedy vs prudent: what the sweep found

1,005 runs (335 configurations × 3 seeds), 20,000 ticks each, produced by

```bash
./build/evosim_sweep --grid all --ticks 20000 --seeds 3 --out-dir analysis/experiments
```

Raw data: [`sweep_summary.csv`](sweep_summary.csv), one row per run, committed.
The companion `sweep_series.csv` (sampled time series, 6.8 MB) is not committed —
rerun the command above to regenerate both.

`greed` is a heritable trait in `[0.05, 1.0]`: the **fraction of a food source's
standing stock an agent takes per visit**. It has no metabolic cost. Its only
cost is ecological — harvesting a source below `collapse_threshold × capacity`
kills it permanently.

---

## 1. There is a sharp sustainability cliff at greed ≈ 0.15–0.20

Survival rate over 3 seeds, founder greed against `recolonise_rate` (how many
dead sources per second come back from outside the system):

| founder greed | rec 0 | 1 | 2 | 5 | 10 | 25 | 50 |
|---:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| 0.05 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 |
| 0.10 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 |
| 0.15 | 1/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 |
| **0.20** | 0/3 | 0/3 | 0/3 | 0/3 | 1/3 | 3/3 | 3/3 |
| 0.25 | 0/3 | 0/3 | 0/3 | 0/3 | 2/3 | 3/3 | 3/3 |
| 0.35 | 0/3 | 0/3 | 0/3 | 0/3 | 3/3 | 3/3 | 3/3 |
| 0.50 | 0/3 | 0/3 | 0/3 | 0/3 | 2/3 | 3/3 | 3/3 |
| 0.75 | 0/3 | 0/3 | 0/3 | 0/3 | 3/3 | 3/3 | 3/3 |
| 1.00 | 0/3 | 0/3 | 0/3 | 0/3 | 2/3 | 3/3 | 3/3 |

The transition is not gradual. At or below 0.15 the population is
**self-sustaining with no external replenishment at all** — it harvests a
renewable resource indefinitely. At 0.20 and above it cannot survive without
sources being replaced from outside, and the greed value above the cliff barely
matters: 0.20 and 1.00 fail the same way.

The rate is not a stand-in for greed either. Above the cliff, survival is bought
entirely by `recolonise_rate` — at 25/s every greed level survives, because the
resource is being restocked faster than any of them can strip it.

## 2. A closed commons is only viable for prudent founders

Across the whole economy grid with `recolonise_rate = 0` (nothing ever comes
back), by founder greed:

| founders | sustained |
|---|---:|
| greed 0.10 | 14/27 |
| greed 0.15 | 9/27 |
| greed 0.25 | 0/27 |
| greed 0.50 | 0/27 |
| greed 0.85 | 0/27 |
| **mixed (uniform 0.05–1.0)** | **0/27** |

Mixed founders never survive a closed commons. The greedy half of the founding
population destroys the resource long before selection can remove them from it.

Regeneration rate does not rescue this — at `recolonise_rate = 0`, survival is
3/54, 11/54 and 9/54 at regen 1.5, 3.0 and 6.0. Faster regrowth raises the
carrying capacity; it does not change who is above the cliff.

## 3. Head to head, prudence wins every time

The grids above test one strategy at a time. This one puts two in the same
world: founders split between greed 0.12 and a greedy value, spatially
interleaved, with greed free to drift.

**60 runs, 60 wins for prudence.** Final mean greed lands between 0.111 and
0.146 in every single run — the greedy lineages are gone — and not one run went
extinct.

| greedy founders | recolonise | prudent share of founders | final mean greed | survived |
|---:|---:|---:|---:|:--:|
| 0.50 | 2 | 10% | 0.123 | 3/3 |
| 0.50 | 2 | 50% | 0.119 | 3/3 |
| 0.50 | 2 | 90% | 0.114 | 3/3 |
| 0.50 | 10 | 10% | 0.119 | 3/3 |
| 0.50 | 10 | 50% | 0.121 | 3/3 |
| 0.50 | 10 | 90% | 0.121 | 3/3 |
| 0.90 | 2 | 10% | 0.127 | 3/3 |
| 0.90 | 2 | 50% | 0.127 | 3/3 |
| 0.90 | 2 | 90% | 0.132 | 3/3 |
| 0.90 | 10 | 10% | 0.121 | 3/3 |
| 0.90 | 10 | 50% | 0.120 | 3/3 |
| 0.90 | 10 | 90% | 0.124 | 3/3 |

A prudent **minority of 10%** is enough — across those 12 runs final greed lands at 0.117–0.130. That is the interesting part: prudence
does not need to start in the majority, it needs only to exist.

The order of events explains why. Tracing one run tick by tick:

| tick | what happens |
|---:|---|
| 0–700 | **Greed wins.** Mean greed climbs 0.51 → 0.62 as greedy lineages out-reproduce prudent ones. Population booms 1,000 → 1,641. |
| 700–1,300 | The resource passes half, then three quarters destroyed. |
| 1,700 | Population crashes. |
| 2,500 | Mean greed falls back under the cliff — the greedy lineages are dying faster than they breed. |
| ~3,000 | Greedy is extinct. Mean greed sits at 0.12. |
| 11,900 | The resource recovers to full, now harvested only by prudent survivors. |

Greed genuinely wins the scramble. It just wins it on the way to destroying the
thing it was winning.

Two caveats worth stating. First, the survivors are few — final populations of
6–64 against a founding 1,000 — because the crash is a severe bottleneck, and
the handful that come through carry whatever foraging traits they happened to
have. Second, this does not contradict section 2: there, mixed founders in a
**fully closed** commons (`recolonise_rate = 0`) always die. Prudence rescues
the world only if the world is still able to regrow at all.

## 4. Greed evolves upward, and space does not stop it

Mean greed drift among surviving mixed-founder populations:

| world | scale | mean drift over 20,000 ticks |
|---|---:|---:|
| 500×500 | 1× | **+0.198** |
| 1000×1000 | 2× | **+0.275** |
| 2000×2000 | 4× | **+0.201** |

I expected the opposite. Offspring are born at the parent's position, so a
lineage is spatially clustered and ought to inherit the patch it ruined; making
the world bigger relative to agent movement should make that feedback stronger
and select *against* greed.

It doesn't. Greed rises at every scale, with no trend across them. A first pass
at one seed per cell appeared to show the effect damping with world size
(+0.071 → +0.067 → +0.043); with three seeds that pattern disappears entirely.
It was noise, and I had a mechanism ready to explain it — which is the reason
the sweep replicates every cell now.

**What this means together with (1) and (2):** prudence is never *reached* by
evolution here, only *inherited*. A population that starts below the cliff stays
there under strong stabilizing selection — greed variance is held near 0.005 in
long runs against a mutation step of 0.048 — because any lineage that drifts
above it destroys its own patch. But a population that starts above the cliff
never evolves down to it. It dies first.

## 5. The model only works because a bite is a discrete visit

Worth recording because it invalidated the first two versions of this
experiment. Without a digestion cooldown, an agent that reaches a source bites
it every tick — 60 times a second — and drains it to death regardless of how
small each bite is. Every source then yields roughly the same total biomass
before dying whatever the harvester's greed, so greed has no ecological
consequence at all, and the measured "sources destroyed per 1000 units
harvested" came out at 43.2 for greed 0.10 against 40.7 for greed 1.00 — a
*reversal*, and tiny.

`food.digest_ticks = 60` makes a bite a visit. With that one change the same
sweep produces the cliff in section 1.

---

## Reproducing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/evosim_sweep --grid all --ticks 20000 --seeds 3 --out-dir analysis/experiments
```

`evosim_sweep` defaults to a quarter of the machine's cores; pass `--jobs` to
change that. Every run is an independent `World` on its own one-thread pool,
which is safe for the same reason the rest of the engine is: the RNG is
stateless and every write is index-stable, so a run's result does not depend on
what else is running beside it. All 1,005 runs are reproducible from their
`(config, seed)` pair, and the `state_hash` column proves it.
