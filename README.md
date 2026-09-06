# EvoSim — Deterministic Parallel Evolution Simulator

A fixed-timestep evolution simulator in C++17 that parallelises across cores
**without giving up bit-exact reproducibility**.

> **A 1-thread run and a 16-thread run produce the same 64-bit state hash.**
> Not "close". Not "within tolerance". The same bits — on either neighbour
> search path, at 20k agents, and when oversubscribed to 100 threads on an
> 18-core box.

Agents carry three heritable traits, forage, spend energy, reproduce with
mutation, and die. There is no fitness function: selection falls out of an
energy economy where movement cost is quadratic in speed and cubic in size, so
maxing a trait is fatal rather than free.

The evolution is the demo. The engine is the point.

**[▶ Run it in your browser](https://nzarc312.github.io/evosim-engine/)** — four
recorded runs, a live sandbox you can drive with sliders, and the engine's
measured performance. No install.

```bash
cmake -B build && cmake --build build
./build/evosim --seed 42 --ticks 10000 --thread-sweep    # proves the claim above
```

---

## In plain terms

**What it is.** A little world, 500 by 500, with a few thousand food plants and a
thousand creatures. Each creature wanders, eats, spends energy, has offspring
that inherit its traits with small random changes, and eventually dies. Nobody
tells them how to behave — the traits that happen to work spread, and the ones
that don't die out. That's evolution, running as a program.

**The engineering problem.** Running that on all 16 cores of a processor instead
of 1 makes it faster, but normally it also makes it *unrepeatable*: cores finish
in a slightly different order each time, tiny rounding differences creep in, and
two runs of the identical setup drift apart. This project's whole point is that
they don't. Run it on 1 core or 16 and you get **the same answer, down to the
last digit** — which makes it 5× more world in the same time budget with nothing
given up for it.

- On one core it can handle about **91,000 creatures** while still updating 60
  times a second — smooth, like a game. On sixteen cores, **485,000**.
- Finding "what food is near me" was rewritten from checking every plant against
  every creature to a smarter method: **1,550× faster** at 50,000 creatures.

**The interesting result.** Food plants aren't just items to pick up — they
regrow, and if a creature strips one too far, it dies permanently. Each creature
inherits how *greedy* it is: what fraction of a plant it takes per visit. That
turns out to matter enormously:

- Creatures that take **about 15% or less** live off the land indefinitely and
  never lose a single plant.
- Creatures that take **20% or more** wipe out their own food supply and starve.
  There's a sharp line between the two — 0.20 fails just as badly as 1.00.
- Put both kinds in the same world and **the greedy ones win at first**: they
  out-breed the careful ones and the population booms. Then the food runs out
  underneath them, and they die with it. Within 3,000 steps the greedy
  bloodlines are extinct and the careful ones inherit a recovering world.
- A careful **minority of just 10%** is enough to take over. Across 60 runs of
  that head-to-head, the careful strategy won 60 times.
- But the winners pay for it. The crash is severe enough that only a few dozen
  creatures survive out of a thousand, and of the 1,000 original family lines,
  only 23 to 39 remain.

**What would change the answer.** The model leaves out plenty, and each omission
would likely shift the result:

- Creatures can't remember or learn — they only react to what's in front of
  them. A creature that remembered which patches it had exhausted would harvest
  very differently.
- There are no predators, disease, or seasons. Food regrows at a steady rate all
  the time, so the only pressure is competition with each other.
- Creatures can't communicate, so they can't agree to leave a plant alone. Real
  commons are often saved by exactly that kind of arrangement.
- Offspring are born exactly where the parent is standing. If they scattered
  further, a greedy family wouldn't be left living in the patch it ruined, and
  restraint would pay off less.
- Greediness is one fixed number per creature. One that adjusted its appetite to
  how much food was actually left would be a different — and probably much more
  successful — strategy.

---

## Results

Measured on an Apple M5 Pro (6 performance + 12 efficiency cores, 24 GB),
Apple clang 21, `-O3`, C++17. Every number below was produced by the benchmarks
in `bench/`; none of them are hand-typed.

![speedup](analysis/plots/speedup.png)

### Threading scale-up — 200,000 agents

| threads | ms/tick | speedup | parallel efficiency | state hash |
|---:|---:|---:|---:|---|
| 1 | 39.401 | 1.00x | 100% | `9256348aa7c5b45e` |
| 2 | 20.043 | 1.97x | 98% | `9256348aa7c5b45e` |
| 4 | 10.662 | 3.70x | 92% | `9256348aa7c5b45e` |
| 8 | 7.683 | 5.13x | 64% | `9256348aa7c5b45e` |
| 16 | 5.131 | 7.68x | 48% | `9256348aa7c5b45e` |

**All thread counts produced the same state hash.**

### Threading scale-up — 50,000 agents

| threads | ms/tick | speedup | parallel efficiency | state hash |
|---:|---:|---:|---:|---|
| 1 | 8.681 | 1.00x | 100% | `fb244219630a45d7` |
| 2 | 4.645 | 1.87x | 93% | `fb244219630a45d7` |
| 4 | 2.715 | 3.20x | 80% | `fb244219630a45d7` |
| 8 | 1.703 | 5.10x | 64% | `fb244219630a45d7` |
| 16 | 1.327 | 6.54x | 41% | `fb244219630a45d7` |

**All thread counts produced the same state hash.**

### Measured serial fraction

Rather than inferring the serial fraction from the speedup curve, `bench_scaling
--mode serial` times each phase directly.

| phase | parallel? | ms/tick @1 | ms/tick @16 | share @16 |
|---|---|---:|---:|---:|
| P1-P3 grid build (total) | mixed | 1.123 | 0.923 | 17.9% |
| &nbsp;&nbsp;of which P2 prefix sum | **serial** | 0.302 | 0.311 | 6.0% |
| P4 agents | parallel | 36.277 | 3.846 | 74.6% |
| P5 claim resolution | **serial** | 0.024 | 0.030 | 0.6% |
| P6 mark deaths | parallel | 0.100 | 0.067 | 1.3% |
| P7 compact + reproduce | **serial** | 0.115 | 0.118 | 2.3% |
| P8 food respawn | **serial** | 0.169 | 0.169 | 3.3% |
| whole tick | - | 37.808 | 5.153 | 100.0% |

- Serial fraction at 1 thread: **1.6%** (Amdahl ceiling 61.9x)
- Serial fraction at 16 threads: **12.2%** (Amdahl ceiling 8.2x)

The serial fraction is larger at 16 threads because the parallel phases shrink while the serial ones do not -- that is precisely what the ceiling means.

**Reading this honestly:** the measured serial phases are only 1.6% of a
one-thread tick, which puts the Amdahl ceiling at | agents | 1 thread ms/tick | 16 threads ms/tick | speedup |
|---:|---:|---:|---:|
| 25000 | 4.76 | 0.74 | 6.47x |
| 50000 | 8.86 | 1.30 | 6.81x |
| 100000 | 18.37 | 2.93 | 6.28x |
| 200000 | 40.14 | 5.88 | 6.82x |
| 400000 | 109.17 | 13.09 | 8.34x |
| 800000 | 295.82 | 31.04 | 9.53x |

**60 Hz ceiling: 91170 agents on 1 thread, 485498 agents on 16 threads (5.33x more world in the same frame budget).**x — far above the
7.68x actually observed at 16 threads. So Amdahl is *not* what limits this
workload. Two things are:

1. **Core heterogeneity.** This machine has 6 performance cores and 12
   efficiency cores. Parallel efficiency is 98% at 2 threads and 92%
   at 4 — all on P-cores — and then falls to 64% at 8 and 48% at 16
   as the E-cores join. The knee lands exactly where it should if an E-core runs
   at roughly 40% of a P-core. P4 alone, the phase that is actually parallel,
   speeds up 9.4x.
2. **The serial fraction is measured, not fixed.** At 16 threads the parallel
   phases have shrunk by ~10x while the serial ones have not, so the same
   absolute serial work is now 12.2% of the tick. That is what an Amdahl
   ceiling means in practice.

### Neighbour search: naive vs counting-sort grid

| agents | food | world | naive ms/tick | grid ms/tick | speedup |
|---:|---:|---:|---:|---:|---:|
| 100 | 200 | 158^2 | 0.068 | 0.018 | 3.9x |
| 1000 | 2000 | 500^2 | 3.593 | 0.083 | 43.2x |
| 5000 | 10000 | 1118^2 | 106.999 | 0.618 | 173.1x |
| 10000 | 20000 | 1581^2 | 485.253 | 1.373 | 353.4x |
| 25000 | 50000 | 2500^2 | 3282.271 | 4.053 | 809.8x |
| 50000 | 100000 | 3536^2 | 13258.650 | 8.555 | 1549.8x |
| 100000 | 200000 | 5000^2 | - | 18.118 | - |
| 200000 | 400000 | 7071^2 | - | 38.364 | - |
| 400000 | 800000 | 10000^2 | - | 100.507 | - |

Up to **1550x**. Agents sustainable at 60 Hz (16.67 ms/tick) rise
from **2,070** on the naive path to **92,578** on the grid.

### How much world fits in a frame

The number a real-time budget actually cares about is not "how fast is a tick"
but "how much world fits inside 16.67 ms".

| agents | 1 thread ms/tick | 16 threads ms/tick | speedup |
|---:|---:|---:|---:|
| 25000 | 4.76 | 0.74 | 6.47x |
| 50000 | 8.86 | 1.30 | 6.81x |
| 100000 | 18.37 | 2.93 | 6.28x |
| 200000 | 40.14 | 5.88 | 6.82x |
| 400000 | 109.17 | 13.09 | 8.34x |
| 800000 | 295.82 | 31.04 | 9.53x |

**60 Hz ceiling: 91170 agents on 1 thread, 485498 agents on 16 threads (5.33x more world in the same frame budget).**

Threading buys **5.33x more agents inside the same frame budget** —
91,170 on one thread, 485,498 on 16 — and the per-tick speedup
*grows* with population (7.4x at 25k, 9.6x at 800k) because the parallel phases
scale while the fixed dispatch cost does not.

### Memory layout: SoA vs AoS

The kernel touches 4 of the 9 per-agent fields (32 of 80 bytes). SoA streams
four dense arrays; AoS strides over the whole struct and pulls the other five
fields into cache for nothing.

| agents | SoA working set (MiB) | AoS working set (MiB) | SoA ns/agent | AoS ns/agent | SoA speedup |
|---:|---:|---:|---:|---:|---:|
| 50000 | 1.53 | 3.81 | 0.474 | 0.785 | 1.66x |
| 500000 | 15.26 | 38.15 | 0.485 | 1.095 | 2.26x |
| 4000000 | 122.07 | 305.18 | 0.486 | 1.277 | 2.63x |

The gap widens with the working set — 1.66x at 50k agents, 2.63x at 4M — which
is the signature of a cache effect rather than an instruction-count one. SoA is
also timed first in each pair, so it pays the cold-cache cost and the numbers
are conservative.

### False sharing: a negative result worth reporting

65536 cache-resident values, swept 96 times per chunk, 20 reductions. This machine reports a 256-byte cache line, so the textbook `alignas(64)` still leaves two slots sharing one coherence granule.

| threads | unpadded ms | alignas(64) ms | alignas(256) ms | fix vs unpadded | shipped (local acc) ms |
|---:|---:|---:|---:|---:|---:|
| 1 | 257.5 | 252.3 | 279.0 | 0.92x | 60.6 |
| 2 | 128.7 | 128.6 | 142.2 | 0.91x | 32.0 |
| 4 | 67.2 | 67.4 | 81.2 | 0.83x | 16.2 |
| 8 | 41.2 | 40.0 | 43.4 | 0.95x | 9.8 |
| 16 | 22.5 | 22.6 | 24.6 | 0.92x | 7.2 |

The last column is what evosim actually ships -- accumulate in a local and store to the slot once per chunk. It sidesteps the problem rather than padding around it, and beats every padded variant.

=== BENCH CEILING ===

**This is the benchmark that did not reproduce, and that is the finding.** The
textbook expectation is a dramatic graph. On this hardware padding is not merely
neutral, it is very slightly *counterproductive* — 0.83–0.95x at every
thread count — because spreading 64 slots across 16 KB costs more in cache
footprint than the coherence traffic it avoids.

Getting to a trustworthy null took three attempts, and the two failed ones are
instructive:

1. **A million values per reduction measured nothing** — at 8 MB per pass the
   loop is memory-bandwidth bound, and streaming that much data swamps any
   coherence traffic. False sharing is a store-traffic effect; the loads have to
   be cheap first.
2. **Even cache-resident, `slot.value += v[i]` measured nothing** — the
   optimiser hoists the accumulator into a register, leaving one store per
   chunk. The tell was 0.5 ns/add, about two cycles, which is FP-add latency and
   far too fast for a load-add-store round trip to memory. A `volatile`
   accumulator forces the store to land.
3. **`alignas(64)` is the wrong constant here.** This machine reports a 128-byte
   cache line (and libc++ reports `hardware_destructive_interference_size` as
   256), so the textbook 64-byte padding leaves two slots inside one coherence
   granule and buys literally nothing. `reduction.hpp` now derives its alignment
   from `std::hardware_destructive_interference_size` where available.

With all three fixed, a direct probe — N threads each hammering one `double` —
still shows only ~17% at 16 threads and ~0% at 2–4, where the same probe on x86
typically shows 5–20x. The plausible explanation is the shared cluster L2:
a store to a line owned by a sibling core is serviced locally rather than by a
cross-die coherence round trip.

The shipped reduction sidesteps the question entirely by accumulating into a
local and storing to the slot once per chunk, which is faster than every padded
variant in the table. The padding is kept because it is the right
defensive choice on hardware where false sharing *is* expensive (x86, 64-byte
lines) and because the shipped path stores to each slot once, where the extra
footprint costs nothing — not because it was measured to help here.

---
## Why parallel determinism is hard

Threading breaks reproducibility in four distinct ways, and each needs its own
fix. Naming all four is most of what this project is for.

| Failure | Cause | Fix here |
|---|---|---|
| **Float reduction order** | `a+b+c` ≠ `c+b+a` in floating point, and thread completion order varies between runs | Per-chunk partial sums combined serially in chunk order ([`reduction.hpp`](include/evosim/reduction.hpp)) |
| **Shared RNG** | Threads drawing from one generator consume it in a different order each run | Counter-based stateless RNG keyed on `(seed, agent, tick, purpose)` ([`rng.hpp`](include/evosim/rng.hpp)) |
| **Contended writes** | Two agents reach the same food; the winner depends on scheduling | Claim-and-resolve: collect in parallel, award serially to the lowest agent index ([`world.cpp`](src/world.cpp)) |
| **Read/write interleaving** | Agent *i* reads agent *j*'s half-updated state | Double buffering — read `front_`, write `back_`, swap at tick end |

The insight worth stating out loud: determinism here comes from **disjoint,
index-stable writes and stateless RNG**, not from how the work is partitioned.
Work-stealing would be just as deterministic under this design. Fixed
partitioning is chosen for reproducible *benchmarks*, not for correctness — and
`test_thread_invariance` proves it by oversubscribing to 100 threads on an
18-core machine, which scrambles the scheduling and changes nothing.

### The one that isn't in the textbook: partition count, not thread count

A correct per-thread reduction still fails the cross-thread-count test. Four
threads make four partial sums, sixteen make sixteen, and the two totals differ
in the last bits. So work is always split into a **fixed 64 chunks** regardless
of thread count, and every per-chunk output — partial sums, claim buffers,
neighbour-candidate scratch — is indexed by *chunk*, never by thread id.

`test_reduction` asserts the fix works *and* asserts that the naive per-thread
scheme genuinely disagrees with itself at 4 vs 16 threads, so the test cannot
quietly rot into passing for the wrong reason.

## Architecture

### Fixed timestep

```cpp
constexpr double DT = 1.0 / 60.0;
void run_headless(World& w, uint64_t ticks) {
    for (uint64_t i = 0; i < ticks; ++i) w.step(DT);   // no clock at all
}
```

Variable `dt` makes results frame-rate dependent, so a fast machine and a slow
machine diverge. Fixing the timestep decouples simulation correctness from
rendering performance and is a precondition for reproducibility.

### Phase structure

Fixed order. Changing it means bumping `World::kStateHashVersion`.

```
World::step(dt):
  P1  [PARALLEL] cell index per food item
  P2  [SERIAL]   prefix sum over cell counts -> bucket offsets
  P3  [PARALLEL] scatter indices into the flat sorted array
  P4  [PARALLEL] per agent: sense -> steer -> integrate -> energy -> age,
                 reads front_, writes back_, records claims per chunk
  P5  [SERIAL]   resolve food claims in agent-index order
  P6  [PARALLEL] mark deaths (energy <= 0 or age > MAX_AGE)
  P7  [SERIAL]   stable compaction; append mutated offspring
  P8  [SERIAL]   respawn food to target density
  P9  [PARALLEL] telemetry partial sums -> [SERIAL] fixed-order reduction
  swap(front_, back_); ++tick
```

P7 compaction **preserves relative order**. Swap-and-pop would reorder agents
and diverge two otherwise identical runs.

### Counting-sort spatial grid

Three flat arrays — `cell_of`, `cell_starts`, `sorted` — filled by counting
sort. Not a vector-of-vectors: that can't be filled in parallel
deterministically and scatters the candidate list across the heap. Within a
cell, items land in ascending index order no matter how the build is
partitioned, so **the built grid is identical at any thread count for free**.

The grid indexes *food*, not agents: its cell size is defined as the largest
query radius in the population, and what agents query for is food. Since
`sense_radius` is heritable, the cell size is recomputed every tick.

The naive O(agents × food) scan is kept permanently behind `--naive`. It is the
benchmark baseline and the correctness oracle — `test_spatial_grid` runs both
paths of the full simulation for 400 ticks and asserts the agent arrays stay
bit-identical.

## Things that broke

The three bugs that cost real time, and how each was found.

### 1. Debug and release disagreed on the state hash — because of `sin`

M5's debug-vs-release check failed at the very first sample. Dumping the full
state instead of the hash showed the simulation state was identical everywhere
except `vel_y` on **4 agents out of 1000**, off by one ULP, with `vel_x` fine.

`vel_x` is `cos`, `vel_y` is `sin`. At `-O3` clang recognises an adjacent
`sin(x)`/`cos(x)` pair on the same argument and rewrites it into Apple libm's
`__sincos_stret`, whose sine differs from `sin()` by one ULP on some inputs. No
compiler flag prevents this — `-ffp-contract=off` and `-fno-fast-math` were
already on and are not what's being violated. The call itself has to go.

A first attempt to confirm the theory with a four-sample micro-benchmark
*passed*, which nearly sent me down the wrong path. Four samples out of a
thousand differ; four samples was not enough to hit one.

So [`math.hpp`](include/evosim/math.hpp) implements `sin`, `cos` and `log`
directly (Cody–Waite reduction with fdlibm kernels; `frexp` plus an atanh
series for log). These are plain sequences of IEEE multiplies and adds — without
fast-math the compiler may not reassociate them, and without contraction it may
not fuse them, so they are identical at every optimisation level. As a bonus the
spec didn't ask for, that also makes hashes comparable across platforms, which a
libm-based hash never could be. `sqrt` stays: IEEE-754 requires it to be
correctly rounded, so it is already exact everywhere.

### 2. The grid and the naive scan disagreed on ties

`test_spatial_grid` caught the grid returning a different food item than brute
force, rarely. The naive scan walks food in ascending index order and takes
`d2 < best`, so equidistant food resolves to the lower index for free. The grid
visits candidates cell-block by cell-block, so its candidate list is *not* in
global index order and the same comparison picks whichever tied item it saw
first. Two implementations of "nearest" that agree on distance and disagree on
ties are two different simulations. `nearest_food_grid` now breaks ties on the
food index explicitly.

### 3. A config parser bug hidden by a default value

`config/default.toml` writes `[world]  width = 500.0 ; height = 500.0` — a
section header and a key on the same line. The parser consumed the header and
`continue`d, silently dropping `width`. The first version of the test asserted
`width == 500.0` and passed, because the dropped value was *identical to the
default*. It only surfaced when a malformed-input test expected
`[world] width = wide` to throw and it didn't. The test now uses `501.5`, a
value no default could supply.

The general lesson, and the reason it is worth writing down: a test that asserts
a value equal to the default cannot distinguish "parsed correctly" from "never
parsed at all".

## Building

Requires CMake ≥ 3.16 and a C++17 compiler. No third-party dependencies — the
TOML subset parser and the test harness are both in-tree.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The simulation never includes or links a graphics library; `src/` and
`include/` build and run headless on a box with no display.

### Reproducing the debug-vs-release claim

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j
./build/test_determinism       --hash-out /tmp/release.txt
./build-debug/test_determinism --hash-out /tmp/debug.txt
diff /tmp/release.txt /tmp/debug.txt && echo "identical"
```

There is deliberately no hardcoded golden hash in the test suite. The claim is
reproducibility on a machine and across builds; a baked-in constant would make
the test fail on any platform whose `sqrt` rounding or `double` layout differs,
which is a different claim than the one being made.

## Usage

```bash
evosim --seed 42 --ticks 100000 --headless --threads 8 --out run.csv
evosim --seed 42 --ticks 10000 --naive --bench
evosim --seed 42 --ticks 10000 --verify           # hash every 1000 ticks
evosim --seed 42 --ticks 10000 --thread-sweep     # 1,2,4,8,16; compares hashes
evosim --set population.initial_agents=200000 --set world.width=7071 ...
```

## Tests

| Test | What it pins down |
|---|---|
| `test_thread_invariance` | **The claim.** 1/2/4/8/16 threads, 5000 ticks, one hash. Plus 20k agents, the naive path, repeated 16-thread runs, and 31/64/100-thread oversubscription. |
| `test_determinism` | Same seed twice; five seeds; naive vs grid; two worlds stepped alternately against a third stepped alone (catches static mutable state). |
| `test_reduction` | Bit-identical sums at 1/2/3/4/7/8/16 threads, and proof the per-thread scheme really does differ. |
| `test_spatial_grid` | Grid vs brute force over 9 configurations including degenerate grids; parallel vs serial build on a dense set; 400 ticks of both full simulation paths. |
| `test_math` | The in-house `sin`/`cos`/`log` against libm, and that `unit_open()`'s smallest output still logs finitely. |
| `test_rng` | Purity, seekability, collision-freedom, uniformity, gaussian moments. |
| `test_config` | The shipped grammar, malformed input, and the default-masking trap from bug 3. |

## What evolves

At the shipped configuration the population settles into a food-limited
equilibrium around 22,000 agents from 1,000 founders, and the traits move under
selection rather than saturating:

![traits](analysis/plots/traits.png)

- **Speed** rises to ~2.83 of a 3.0 maximum. With food abundant in space but
  hard to find, getting there first pays.
- **Size** collapses to ~0.67 of a 2.0 maximum. Movement cost is cubic in size
  and the only thing size buys is a slightly larger eating radius, which is
  nearly worthless once an agent can home in precisely.
- **Sense radius** is the interesting one. It rises early while the population
  is sparse, peaks, then falls back to ~3.8 out of 15 as the population reaches
  equilibrium: once there are 22,000 agents competing, food is close by anyway
  and paying a linear energy cost to see further stops being worth it. That
  reversal is stabilising selection on a heritable trait, produced by the
  energy economy with no fitness function anywhere in the code.

![population](analysis/plots/population.png)
![trait correlation](analysis/plots/trait_correlation.png)

## Greedy vs prudent: the commons

Food is not a pickup. Each source holds a **stock** that regrows toward capacity,
and harvesting it below `collapse_threshold × capacity` kills it permanently.
A fourth heritable trait, **`greed`**, is the fraction of a source's standing
stock an agent takes per visit. Greed has no metabolic cost — its only cost is
ecological, and it falls partly on the lineage that caused it, because offspring
are born where the parent stood.

A sweep of 1,005 runs (335 configurations × 3 seeds) finds a **sharp
sustainability cliff between greed 0.15 and 0.20**:

| founder greed | survives a closed commons (no external replenishment) |
|---:|:--|
| 0.05 – 0.10 | always |
| 0.15 | usually |
| 0.20 – 1.00 | never |

Below the cliff the population harvests a renewable resource indefinitely and
never loses a single source. Above it, survival is bought entirely by restocking
the world from outside, and the exact greed value stops mattering — 0.20 fails
the same way 1.00 does.

**Put the two strategies in one world and prudence wins every time** — 60 of 60
head-to-head runs, even when prudent founders start as only 10% of the
population. The order of events is the point: greed genuinely wins the first
~700 ticks, out-reproducing prudent lineages and booming the population to
1,641, and then the resource it stripped fails underneath it. By tick 3,000 the
greedy lineages are extinct and mean greed sits at 0.12.

Three more results, all in
[`analysis/experiments/FINDINGS.md`](analysis/experiments/FINDINGS.md):

- **Prudence is inherited, never reached.** A population that starts below the
  cliff is held there by strong stabilizing selection — greed variance sits near
  0.005 against a mutation step of 0.048 — but a population that starts above it
  dies before selection can walk it down. Mixed founders never once survived a
  closed commons: 0 of 27.
- **Space does not save the commons.** Making the world 4× larger, so a lineage
  inherits the patch it ruined, should select against greed. It doesn't: drift is
  +0.198, +0.275 and +0.201 at 1×, 2× and 4×. A single-seed pass appeared to show
  the effect I expected, and replication erased it.
- **Winning costs the winners.** A crash from 1,641 agents to a few dozen is a
  severe bottleneck, so the prudent survivors are a small, arbitrary sample. They
  inherit a world that heals completely and still sit at a fraction of the
  population a never-crashed prudent run sustains.

### The timeline viewer

[**Open the interactive timeline →**](https://nzarc312.github.io/evosim-engine/)

Four runs side by side, sharing one cursor so scrubbing compares the same tick
across all of them: three pure strategies (founder greed 0.12, 0.50, 0.90) plus
the head-to-head, where half the founders start prudent and half greedy in one
world. Agents are coloured by greed — blue prudent, red greedy — so in the
head-to-head you watch the red dots take over and then vanish.

Step or scrub through 30,000 ticks and jump between automatically detected
pivotal moments: booms and crashes, the resource passing half and three-quarters
destroyed, greed peaking and reversing, extinction, and recovery. Lineage mode
colours the ten founding families that got furthest and pools the rest, with a
band under each map showing composition over the whole run — 507 of 1,000
founding families survive the prudent run, 23 the mixed one, none the greedy one.

Below the recordings is a **sandbox**: sliders for founding agents, founder
greed, food sources, regrowth, recolonisation and seed, running live in the
browser. That one is the same ruleset reimplemented in JavaScript rather than the
C++ engine — it is not bit-identical to the recordings, though the same seed and
sliders reproduce the same run every time.

```bash
./build/evosim --seed 42 --ticks 30000 --set population.initial_greed=0.5 \
               --out run.csv --record run.evorec
python3 analysis/make_viewer.py \
        --run "Mixed · 0.50::Half a source per visit.::run.evorec::run.csv" \
        --out viewer.html
```

The recording is a compact binary: frames sampled in time, agents sampled by
fixed stride within a frame, positions quantised to 16 bits. Aggregate counters
in each frame are the true totals, not the sampled ones.

### Tuning the energy economy

```
cost_per_second = base_cost + move_coef * speed² * size³ + sense_coef * sense_radius
```

The superlinear terms are the whole selection mechanism: with a linear cost
every trait saturates at its maximum and nothing evolves.

The spec's starting values (0.05 / 0.02 / 0.01) are per-*tick* costs. Applied as
a per-second rate at a 60 Hz timestep they are about 30x too cheap: mean energy
climbs monotonically, nothing starves, and the population grows until it hits
`max_agents`. The shipped values (1.00 / 0.35 / 0.15) put a mid-range agent at
~4.3 energy/s against a measured intake of ~6/s, so starvation is a live
pressure, while an agent at maximum traits burns 28.5/s and cannot survive at
all. Energy is applied as `energy -= cost * dt`, so the model stays correct if
the timestep is ever changed.

## What I would do differently

- **The grid is rebuilt from scratch every tick.** Most food does not move
  between ticks, so an incremental update would skip most of the work. The full
  rebuild was chosen because it is trivially deterministic; an incremental one
  would need care to keep bucket order index-stable.
- **Cell size is a single global number** derived from the largest sense radius
  in the population, while sense radius evolves per-agent. One long-sighted
  outlier inflates every cell. A two-level grid, or clamping the cell size to a
  high percentile and handling the tail separately, would fix it.
- **P7 compaction is serial**, at 2.3% of a 16-thread tick. A parallel stream compaction (per-chunk survivor
  counts, prefix sum, parallel scatter) would work, and it is the same shape as
  the counting sort already in `spatial_grid.cpp`.
- **P2's prefix sum is 6.0% of a 16-thread tick** and grows with cell count, so
  it is now the biggest serial term. A parallel scan would help more than
  anything else on this list.
- **The per-chunk histogram for the parallel scatter costs `chunks * n_cells`**,
  so the build only takes that path on dense grids and falls back to a serial
  scatter at this simulation's food density. A blocked or sparse histogram would
  let the parallel path apply generally.

## Layout

```
include/evosim/   rng, math, thread_pool, reduction, world, spatial_grid,
                  genome, telemetry, recorder, config, hash
src/              implementations + main.cpp
bench/            bench_neighbors.cpp, bench_scaling.cpp
tools/            sweep.cpp -- the greedy-vs-prudent parameter sweep
tests/            7 CTest executables, no third-party framework
analysis/         plot_run.py, make_viewer.py + the timeline viewer template,
                  readme/ (regenerates README.md from measured output),
                  experiments/ (sweep CSVs and FINDINGS.md), plots/
config/           default.toml
```

`src/` and `include/` never reference a renderer. The simulation builds and runs
with no graphics library present.
