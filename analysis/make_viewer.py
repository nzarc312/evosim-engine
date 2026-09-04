#!/usr/bin/env python3
"""Package a recording + telemetry into a self-contained timeline viewer.

    python3 analysis/make_viewer.py --record run.evorec --telemetry run.csv \
                                    --out analysis/viewer.html

The recording carries the frames; the telemetry CSV (full resolution, every 100
ticks) is what the pivotal-moment detector reads, because a 240-frame recording
is too coarse to locate the onset of a crash.
"""

import argparse
import base64
import csv
import json
import os
import pathlib
import re
import struct
import sys

def read_record(path):
    """Parse the .evorec container. Returns (meta, raw_bytes_of_frames)."""
    with open(path, "rb") as f:
        blob = f.read()

    magic = blob[:8]
    if magic != b"EVOREC01":
        sys.exit(f"{path}: not an evorec file (magic {magic!r})")

    off = 8
    world_w, world_h, capacity = struct.unpack_from("<3d", blob, off); off += 24
    total_ticks, every = struct.unpack_from("<2Q", blob, off); off += 16
    max_agents, n_sources = struct.unpack_from("<2I", blob, off); off += 8
    (n_frames,) = struct.unpack_from("<Q", blob, off); off += 8

    meta = dict(world_w=world_w, world_h=world_h, capacity=capacity,
                total_ticks=total_ticks, every=every, max_agents=max_agents,
                n_sources=n_sources, n_frames=n_frames)

    # Index the frames so the viewer can seek without re-walking the buffer.
    offsets, ticks, pops, srcs, greeds = [], [], [], [], []
    p = off
    for _ in range(n_frames):
        offsets.append(p - off)
        # 40-byte header: tick(8) pop(4) sources(4) + five f32 (20) + nAgents(4)
        tick, pop, src = struct.unpack_from("<QII", blob, p)
        mg = struct.unpack_from("<f", blob, p + 16)[0]
        (n_samp,) = struct.unpack_from("<I", blob, p + 36)
        ticks.append(tick); pops.append(pop); srcs.append(src); greeds.append(mg)
        p += 40 + n_samp * 7 + n_sources * 5
    if p != len(blob):
        print(f"warning: {len(blob) - p} trailing bytes in {path}", file=sys.stderr)

    meta.update(offsets=offsets, ticks=ticks, populations=pops,
                sources=srcs, mean_greed=[round(g, 5) for g in greeds])
    return meta, blob[off:p]


def lineage_profile(frames, meta, top_k=10):
    """Per-frame lineage composition.

    Colouring 1,000 founding lineages with 1,000 hues shows nothing: it is
    confetti, and no viewer can tell which family is which. What is worth seeing
    is which lineages actually take over, so this ranks them by peak share and
    keeps the top `top_k`; everything else is pooled into one neutral bucket.

    Counts come from the sampled agents in the recording, so shares are exact
    once the population fits in the sample (which it does for most of every run
    here) and a fixed-stride estimate before that.
    """
    n_sources = meta["n_sources"]
    per_frame = []          # list of {lineage: count}
    alive = []              # distinct lineages present
    for off in meta["offsets"]:
        n_agents = struct.unpack_from("<I", frames, off + 36)[0]
        base = off + 40
        counts = {}
        for i in range(n_agents):
            lin = struct.unpack_from("<H", frames, base + i * 7 + 5)[0]
            counts[lin] = counts.get(lin, 0) + 1
        per_frame.append(counts)
        alive.append(len(counts))

    peak = {}
    for counts in per_frame:
        total = sum(counts.values()) or 1
        for lin, c in counts.items():
            peak[lin] = max(peak.get(lin, 0.0), c / total)
    # Rank by peak share, break ties on lineage id so the palette assignment is
    # reproducible rather than dependent on dict ordering.
    top = sorted(peak, key=lambda l: (-peak[l], l))[:top_k]
    index = {lin: k for k, lin in enumerate(top)}

    shares = []
    for counts in per_frame:
        total = sum(counts.values()) or 1
        row = [0.0] * (len(top) + 1)
        for lin, c in counts.items():
            row[index.get(lin, len(top))] += c / total
        shares.append([round(v, 4) for v in row])

    # Every founder starts its own lineage, so the founding family count is just
    # the founding population -- and frame 0 is captured before the first step,
    # so it holds exactly that. (max(alive) would be "families visible in the
    # sample", an undercount; the telemetry's first row is already tick 100, by
    # which point a fast run has grown.)
    return dict(top=top, shares=shares, alive=alive,
                founders=int(meta["populations"][0]) if meta["populations"] else 0)


def read_telemetry(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    out = {k: [] for k in rows[0]}
    for r in rows:
        for k, v in r.items():
            try:
                out[k].append(float(v))
            except ValueError:
                out[k].append(float("nan"))
    return out


def detect_events(t, source_slots):
    """Locate the moments a reader would want to jump straight to.

    Every rule is stated in terms the simulation actually has -- population
    multiples, fraction of sources still alive -- so a marker always means one
    specific, checkable thing rather than 'something happened here'.
    """
    tick = t["tick"]
    pop = t["population"]
    src = t["food_active"]
    greed = t["mean_greed"]
    n = len(tick)
    ev = []

    def add(i, kind, label, detail):
        ev.append(dict(tick=int(tick[i]), kind=kind, label=label, detail=detail))

    # --- extinction: the run ends, everything after is an empty world ---
    for i in range(n):
        if pop[i] == 0:
            add(i, "extinction", "Extinction",
                f"Population reaches zero at tick {int(tick[i]):,}.")
            break

    # --- peak population ---
    # Only worth a marker if the world actually grew past what it was founded
    # with. Otherwise the "peak" is just tick 0 and says nothing.
    if n and pop[0] > 0:
        i = max(range(n), key=lambda k: pop[k])
        if pop[i] > 1.15 * pop[0] and tick[i] > 1000:
            add(i, "peak", "Peak population",
                f"{int(pop[i]):,} agents, {pop[i] / pop[0]:.1f}x the founding population.")
        else:
            # No growth phase: the interesting extreme is the low-water mark.
            j = min(range(n), key=lambda k: pop[k] if pop[k] > 0 else 1e18)
            if pop[j] > 0 and pop[j] < 0.5 * pop[0] and tick[j] > 500:
                add(j, "peak", "Low-water mark",
                    f"Population bottoms out at {int(pop[j]):,}, "
                    f"{100 * pop[j] / pop[0]:.0f}% of the founders.")

    # --- booms and crashes, measured over a 2000-tick window ---
    span = max(1, int(2000 / (tick[1] - tick[0]))) if n > 1 else 1
    for i in range(n - span):
        a, b = pop[i], pop[i + span]
        if a >= 50 and b >= 3 * a:
            ev.append(dict(tick=int(tick[i]), kind="boom", label="Population boom",
                           detail=f"{int(a):,} to {int(b):,} agents in 2,000 ticks "
                                  f"({b / a:.1f}x).", _mag=b / a))
        if a >= 100 and b <= 0.4 * a:
            ev.append(dict(tick=int(tick[i]), kind="crash", label="Population crash",
                           detail=f"{int(a):,} down to {int(b):,} agents in 2,000 ticks "
                                  f"(-{100 * (1 - b / a):.0f}%).", _mag=a / max(b, 1)))

    # --- the commons itself ---
    for frac, name in ((0.5, "half"), (0.25, "three quarters")):
        for i in range(n):
            if src[i] < frac * source_slots:
                add(i, "resource", f"Resource down {name}",
                    f"{int(source_slots - src[i]):,} of {int(source_slots):,} food sources "
                    f"harvested to death.")
                break
    below = False
    for i in range(n):
        if src[i] < 0.5 * source_slots:
            below = True
        elif below and src[i] > 0.9 * source_slots:
            add(i, "recovery", "Resource recovers",
                f"Live sources back to {int(src[i]):,} of {int(source_slots):,} after "
                f"the collapse.")
            below = False

    # --- the turning point: greed peaks, then gives up ground ---
    # Only counts as a reversal if greed later falls well below the peak, so a
    # noisy wobble at the top of the run does not earn a marker.
    live = [i for i in range(n) if pop[i] > 0]
    if live:
        pk = max(live, key=lambda k: greed[k])
        after = [greed[i] for i in live if i > pk]
        if after and greed[pk] - min(after) > 0.15 and tick[pk] > 200:
            add(pk, "reversal", "Greed peaks, then loses",
                f"Mean greed tops out at {greed[pk]:.2f} and falls to "
                f"{min(after):.2f} after this point: the greedy strategy has won "
                f"the scramble and is now dying with the resource it stripped.")

    # --- greed crossing the measured sustainability cliff ---
    # 0.17 is where the founder-greed sweep flips from sustained to collapsed;
    # see analysis/experiments/sweep_summary.csv.
    CLIFF = 0.17
    for i in range(1, n):
        if pop[i] == 0 or pop[i - 1] == 0:
            continue
        if greed[i - 1] < CLIFF <= greed[i]:
            add(i, "greed-up", "Greed crosses the cliff",
                f"Mean greed passes {CLIFF:.2f}, the level above which this world "
                f"collapsed in every swept configuration.")
        elif greed[i - 1] >= CLIFF > greed[i]:
            add(i, "greed-down", "Greed falls below the cliff",
                f"Mean greed drops back under {CLIFF:.2f}.")

    # Keep the strongest boom/crash in any 4000-tick neighbourhood; a single
    # event otherwise fires on every sample across its own slope.
    kept = []
    for e in sorted(ev, key=lambda e: (-e.get("_mag", 0), e["tick"])):
        if e["kind"] in ("boom", "crash"):
            if any(k["kind"] == e["kind"] and abs(k["tick"] - e["tick"]) < 4000 for k in kept):
                continue
        kept.append(e)
    for e in kept:
        e.pop("_mag", None)
    return sorted(kept, key=lambda e: e["tick"])


def parse_bench(path):
    """Pull the benchmark output apart into sections, tables and headline numbers.

    The viewer renders whatever is here, so the performance panel and the
    README quote the same measured output and cannot drift apart.
    """
    text = pathlib.Path(path).read_text()
    sections, cur = [], None
    for line in text.splitlines():
        if line.startswith("### "):
            cur = dict(title=line[4:].strip(), prose=[], tables=[])
            sections.append(cur)
            continue
        if cur is None:
            continue
        if line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if all(set(c) <= set("-: ") for c in cells) and cells:
                continue                      # separator row
            if cur["tables"] and cur["tables"][-1]["open"]:
                cur["tables"][-1]["rows"].append(cells)
            else:
                cur["tables"].append(dict(headers=cells, rows=[], open=True))
        else:
            if cur["tables"]:
                cur["tables"][-1]["open"] = False
            if line.strip():
                cur["prose"].append(line.strip())
    for sec in sections:
        for t in sec["tables"]:
            t.pop("open", None)

    def find(frag):
        for sec in sections:
            if frag.lower() in sec["title"].lower():
                return sec
        return None

    head = {}
    m = re.search(r"60 Hz ceiling: ([\d.]+) agents on 1 thread, ([\d.]+) agents on (\d+) threads "
                  r"\(([\d.]+)x", text)
    if m:
        head.update(ceiling_1=int(float(m.group(1))), ceiling_n=int(float(m.group(2))),
                    ceiling_threads=int(m.group(3)), ceiling_ratio=float(m.group(4)))
    m = re.search(r"Serial fraction at 1 thread: \*\*([\d.]+)%\*\* \(Amdahl ceiling ([\d.]+)x\)", text)
    if m:
        head.update(serial_1=float(m.group(1)), amdahl=float(m.group(2)))
    m = re.search(r"Serial fraction at 16 threads: \*\*([\d.]+)%\*\*", text)
    if m:
        head.update(serial_16=float(m.group(1)))

    # speedup curves, straight from the scale-up tables
    curves = []
    for sec in sections:
        m = re.search(r"Threading scale-up:\s*(\d+)\s*agents", sec["title"])
        if m and sec["tables"]:
            t = sec["tables"][0]
            curves.append(dict(
                agents=int(m.group(1)),
                threads=[int(r[0]) for r in t["rows"]],
                ms=[float(r[1]) for r in t["rows"]],
                speedup=[float(r[2].rstrip("x")) for r in t["rows"]],
                hashes=sorted({r[4] for r in t["rows"]}) if len(t["rows"][0]) > 4 else []))
    if curves:
        big = max(curves, key=lambda c: c["agents"])
        head.update(top_speedup=big["speedup"][-1], top_agents=big["agents"],
                    top_ms1=big["ms"][0], top_ms16=big["ms"][-1],
                    one_hash=len(big["hashes"]) == 1)

    nb = find("Neighbour search")
    if nb and nb["tables"]:
        sp = [float(r[5].rstrip("x")) for r in nb["tables"][0]["rows"] if r[5] not in ("-", "")]
        if sp:
            head["grid_speedup"] = max(sp)

    return dict(sections=sections, head=head, curves=curves)


def build_run(label, blurb, record_path, telemetry_path):
    meta, frames = read_record(record_path)
    tel = read_telemetry(telemetry_path)

    slots = max(max(tel["food_active"]) if tel["food_active"] else 0, meta["n_sources"])
    events = detect_events(tel, slots)

    step = max(1, len(tel["tick"]) // 700)      # keep the strip chart light
    series = {k: [round(v, 4) for v in tel[k][::step]]
              for k in ("tick", "population", "food_active", "mean_greed",
                        "std_greed", "stock_total", "mean_sense", "mean_speed")}

    run = dict(label=label, blurb=blurb,
               meta={k: v for k, v in meta.items() if k != "offsets"},
               offsets=meta["offsets"], events=events, series=series,
               source_slots=int(slots),
               lineage=lineage_profile(frames, meta))
    return run, frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", action="append", required=True, metavar="LABEL::BLURB::REC::CSV",
                    help="one run, fields separated by '::'. Repeatable.")
    ap.add_argument("--template", default=os.path.join(os.path.dirname(__file__), "viewer_template.html"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--title", default="")
    ap.add_argument("--caption", default="")
    ap.add_argument("--standfirst", default="")
    ap.add_argument("--bench", default="")
    args = ap.parse_args()

    runs, blobs, base = [], [], 0
    for spec in args.run:
        parts = spec.split("::")
        if len(parts) != 4:
            sys.exit(f"--run needs LABEL::BLURB::RECORD::TELEMETRY, got {spec!r}")
        label, blurb, rec, csv_path = parts
        run, frames = build_run(label, blurb, rec, csv_path)
        # Every run's frames live in one shared buffer; `base` is where this
        # run starts, and its own offsets are relative to that.
        run["base"] = base
        base += len(frames)
        runs.append(run)
        blobs.append(frames)

    payload = dict(runs=runs, caption=args.caption, standfirst=args.standfirst,
                   perf=parse_bench(args.bench) if args.bench else None)
    combined = b"".join(blobs)

    with open(args.template) as f:
        html = f.read()
    html = (html
            .replace("__PAYLOAD__", json.dumps(payload, separators=(",", ":")))
            .replace("__FRAMES_B64__", base64.b64encode(combined).decode("ascii")))
    if args.title:
        html = html.replace("<title>Commons Timeline</title>", f"<title>{args.title}</title>")

    with open(args.out, "w") as f:
        f.write(html)

    print(f"{args.out}  ({os.path.getsize(args.out) / 1048576:.2f} MiB, {len(runs)} runs)")
    for r in runs:
        L = r["lineage"]
        print(f"  {r['label']}: {r['meta']['n_frames']} frames, {len(r['events'])} events, "
              f"families {L['founders']} -> {L['alive'][-1]}, "
              f"top share {max(L['shares'][-1][:-1]) * 100:.0f}%")
        for e in r["events"]:
            print(f"     tick {e['tick']:>7,}  {e['kind']:<12} {e['label']}")


if __name__ == "__main__":
    main()
