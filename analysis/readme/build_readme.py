#!/usr/bin/env python3
"""Regenerate README.md, splicing in the measured benchmark tables.

    python3 analysis/readme/build_readme.py

Numbers quoted in the prose are pulled out of the benchmark output too, so the
tables and the sentences about them cannot drift apart.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
bench = (ROOT / "analysis/experiments/bench_results.txt").read_text()

def section(title_re, text=bench):
    """Return one '### ...' block, without its heading line.

    Line-based on purpose: the regex version of this backtracked its way past
    the heading and returned three newlines for the last section in the file.
    """
    pat = re.compile(title_re)
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if not line.startswith("### "):
            continue
        if start is not None:
            return "\n".join(lines[start:i]).strip("\n")
        if pat.search(line[4:]):
            start = i + 1
    if start is not None:
        return "\n".join(lines[start:]).strip("\n")
    sys.exit(f"could not find a '### ' section matching {title_re!r}")


def table_rows(sec):
    return [l for l in sec.splitlines() if l.startswith("|") and not l.startswith("|-")][1:]

def num(row, col):
    return row.split("|")[col + 1].strip()

scaling200 = section(r"Threading scale-up: 200000")
scaling50  = section(r"Threading scale-up: 50000")
serial     = section(r"Measured serial fraction")
neighbours = section(r"Neighbour search")
layout     = section(r"Memory layout")
ceiling    = section(r"Agents sustainable at 60 Hz")
sharing    = section(r"False sharing")

# Numbers quoted in prose, pulled from the tables so they cannot drift.
r200 = table_rows(scaling200)
sp16 = num(r200[-1], 2)                      # e.g. "7.39x"
ms1, ms16 = num(r200[0], 1), num(r200[-1], 1)
sf = re.search(r"Serial fraction at 1 thread: \*\*([\d.]+)%\*\* \(Amdahl ceiling ([\d.]+)x\)", serial)
sf16 = re.search(r"Serial fraction at 16 threads: \*\*([\d.]+)%\*\*", serial)
nrows = table_rows(neighbours)
best_speedup = max(float(num(r, 5).rstrip("x")) for r in nrows if num(r, 5) != "-")
sixty = re.search(r"naive ([\d]+), grid ([\d]+)", neighbours)
# The prose below the table restates this line; keep it in one place.
neighbours = re.sub(r"\n\*\*Agents sustainable.*", "", neighbours, flags=re.S).rstrip()
lrows = table_rows(layout)
soa_best = num(lrows[-1], 5)
m = re.search(r"60 Hz ceiling: ([\d.]+) agents on 1 thread, ([\d.]+) agents on (\d+) threads "
              r"\(([\d.]+)x", bench)
ceil_1, ceil_n, ceil_t, ceil_r = m.group(1), m.group(2), m.group(3), m.group(4)

fsrows = table_rows(sharing)
fs_ratios = [float(num(r, 4).rstrip("x")) for r in fsrows]
fs_lo, fs_hi = f"{min(fs_ratios):.2f}", f"{max(fs_ratios):.2f}"

eff2, eff4 = num(r200[1], 3), num(r200[2], 3)
eff8, eff16 = num(r200[3], 3), num(r200[4], 3)

srows = table_rows(serial)
p2_share = num(srows[1], 4)
p7_share = num(srows[5], 4)
p4_1, p4_16 = float(num(srows[2], 2)), float(num(srows[2], 3))
p4_speedup = f"{p4_1 / p4_16:.1f}x"
soa_50k = num(lrows[0], 5)

vals = dict(sp16=sp16, ms1=ms1, ms16=ms16,
            serial1=sf.group(1), ceiling=sf.group(2), serial16=sf16.group(1),
            grid_speedup=f"{best_speedup:.0f}",
            naive60=f"{int(sixty.group(1)):,}", grid60=f"{int(sixty.group(2)):,}",
            soa=soa_best, soa_50k=soa_50k, fs_lo=fs_lo, fs_hi=fs_hi,
            ceiling_tbl=ceiling, ceil_1=f"{int(float(ceil_1)):,}",
            ceil_n=f"{int(float(ceil_n)):,}", ceil_t=ceil_t, ceil_r=ceil_r,
            eff2=eff2, eff4=eff4, eff8=eff8, eff16=eff16,
            p2_share=p2_share, p7_share=p7_share, p4_speedup=p4_speedup,
            scaling200=scaling200, scaling50=scaling50, serial_tbl=serial,
            neighbours=neighbours, layout=layout, sharing=sharing)

tpl = (ROOT / "analysis/readme/README.template.md").read_text()
out = re.sub(r"\{\{(\w+)\}\}", lambda m: str(vals[m.group(1)]), tpl)
missing = re.findall(r"\{\{\w+\}\}", out)
if missing:
    sys.exit(f"unsubstituted placeholders: {missing}")
(ROOT / "README.md").write_text(out)
print(f"README.md written ({len(out.splitlines())} lines)")
print("key numbers:", {k: v for k, v in vals.items() if not isinstance(v, str) or len(v) < 20})
