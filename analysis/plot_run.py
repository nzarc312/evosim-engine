#!/usr/bin/env python3
"""Turn an evosim run into the four plots that go in the README.

    python3 analysis/plot_run.py --telemetry run.csv \
                                 --agents agents.csv \
                                 --scaling bench.txt \
                                 --outdir analysis/plots

--telemetry is the CSV written by `evosim --out`.
--agents    is the per-agent dump from `evosim --dump-agents` (optional; without
            it the trait-correlation plot falls back to the population means
            over time, which shows the trajectory but not the spread).
--scaling   is the stdout of bench_scaling; the markdown tables are parsed
            directly so the numbers in the plots are the same ones in the
            README, with no hand transcription in between.
"""

import argparse
import csv
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

TRAITS = [("speed", "Speed", "#4c72b0"),
          ("size", "Size", "#dd8452"),
          ("sense", "Sense radius", "#55a868")]


def read_telemetry(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        sys.exit(f"{path}: no data rows")
    out = {k: [] for k in rows[0]}
    for r in rows:
        for k, v in r.items():
            out[k].append(float(v) if v not in ("", None) else float("nan"))
    return out


def read_agents(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    return {k: [float(r[k]) for r in rows] for k in rows[0]} if rows else None


def parse_scaling(path):
    """Pull `### Threading scale-up: N agents` tables out of bench_scaling output."""
    header = re.compile(r"Threading scale-up:\s*(\d+)\s*agents")
    row = re.compile(r"^\|\s*(\d+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)x\s*\|")
    series, current = {}, None
    with open(path) as f:
        for line in f:
            m = header.search(line)
            if m:
                current = int(m.group(1))
                series[current] = {"threads": [], "ms": [], "speedup": []}
                continue
            m = row.match(line.strip())
            if m and current is not None:
                series[current]["threads"].append(int(m.group(1)))
                series[current]["ms"].append(float(m.group(2)))
                series[current]["speedup"].append(float(m.group(3)))
    return {k: v for k, v in series.items() if v["threads"]}


def style(ax, title, xlabel, ylabel):
    ax.set_title(title, fontsize=11, loc="left")
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.grid(alpha=0.25, linewidth=0.6)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)


def plot_population(t, outdir):
    fig, ax = plt.subplots(figsize=(9, 4.2))
    ax.plot(t["tick"], t["population"], color="#4c72b0", linewidth=1.4, label="population")
    style(ax, "Population over time", "tick", "agents")
    ax2 = ax.twinx()
    ax2.plot(t["tick"], t["food_active"], color="#c44e52", linewidth=1.0,
             alpha=0.6, label="active food")
    ax2.set_ylabel("active food")
    ax2.spines["top"].set_visible(False)
    lines = ax.get_lines() + ax2.get_lines()
    ax.legend(lines, [l.get_label() for l in lines], frameon=False, loc="best")
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "population.png"), dpi=140)
    plt.close(fig)


def plot_traits(t, outdir):
    fig, axes = plt.subplots(3, 1, figsize=(9, 8), sharex=True)
    for ax, (key, label, colour) in zip(axes, TRAITS):
        mean = t["mean_" + key]
        sd = t["std_" + key]
        lo = [m - s for m, s in zip(mean, sd)]
        hi = [m + s for m, s in zip(mean, sd)]
        ax.fill_between(t["tick"], lo, hi, color=colour, alpha=0.22, linewidth=0)
        ax.plot(t["tick"], mean, color=colour, linewidth=1.5)
        style(ax, f"{label}: population mean with ±1σ", "", label)
    axes[-1].set_xlabel("tick")
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "traits.png"), dpi=140)
    plt.close(fig)


def plot_correlation(t, agents, outdir):
    fig, axes = plt.subplots(1, 3, figsize=(12, 4))
    pairs = [("speed", "size"), ("speed", "sense"), ("size", "sense")]
    for ax, (a, b) in zip(axes, pairs):
        if agents:
            ax.scatter(agents[a], agents[b], s=4, alpha=0.18, color="#4c72b0", linewidths=0)
            style(ax, f"{a} vs {b} (final population)", a, b)
        else:
            sc = ax.scatter(t["mean_" + a], t["mean_" + b], c=t["tick"],
                            cmap="viridis", s=8, linewidths=0)
            style(ax, f"mean {a} vs mean {b} (over time)", a, b)
            fig.colorbar(sc, ax=ax, label="tick")
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "trait_correlation.png"), dpi=140)
    plt.close(fig)


def plot_speedup(series, outdir):
    fig, ax = plt.subplots(figsize=(7, 5))
    threads = sorted({n for s in series.values() for n in s["threads"]})
    ax.plot(threads, threads, "--", color="#888888", linewidth=1.2, label="ideal (linear)")
    for agents in sorted(series):
        s = series[agents]
        ax.plot(s["threads"], s["speedup"], marker="o", linewidth=1.6,
                label=f"{agents:,} agents")
    ax.set_xscale("log", base=2)
    ax.set_xticks(threads)
    ax.get_xaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
    style(ax, "Threading speedup (identical state hash at every point)",
          "threads", "speedup vs 1 thread")
    ax.legend(frameon=False)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "speedup.png"), dpi=140)
    plt.close(fig)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--telemetry", required=True)
    p.add_argument("--agents")
    p.add_argument("--scaling")
    p.add_argument("--outdir", default="analysis/plots")
    args = p.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    t = read_telemetry(args.telemetry)
    agents = read_agents(args.agents) if args.agents else None

    plot_population(t, args.outdir)
    plot_traits(t, args.outdir)
    plot_correlation(t, agents, args.outdir)
    written = ["population.png", "traits.png", "trait_correlation.png"]

    if args.scaling:
        series = parse_scaling(args.scaling)
        if series:
            plot_speedup(series, args.outdir)
            written.append("speedup.png")
        else:
            print(f"warning: no scale-up tables found in {args.scaling}", file=sys.stderr)

    for name in written:
        print(os.path.join(args.outdir, name))


if __name__ == "__main__":
    main()
