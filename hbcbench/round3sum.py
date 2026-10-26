#!/usr/bin/env python3
"""Summarize round3.out: random50, taken-branch knee sweep, pollution test.

Layout-cancelled ratio = geometric mean of the original- and swapped-layout
mean ratios (placement effects flip between layouts; a hint effect would not).
Pollution: B cost per group = (pol - polA) / G, from the same run, for b and
bc filler; the hint helps if B costs less with bc filler in both layouts.
"""
import collections
import math
import re
import statistics as st
import sys

runs = collections.defaultdict(list)   # (mode, kind, swapped) -> [ {kernel: (b, bc)} ]
for line in open(sys.argv[1] if len(sys.argv) > 1 else "round3.out"):
    m = re.match(r"=== (\w+) (\w+) (\w+) pass", line)
    if m:
        cur = {}
        runs[(m.group(1), m.group(2), m.group(3).endswith("_swapped"))].append(cur)
        continue
    f = line.split()
    if len(f) >= 4 and re.match(r"(data-|tk|pol)", f[0]):
        try:
            cur[f[0]] = (float(f[1]), float(f[2]))
        except ValueError:
            pass


def ms(xs):
    return st.mean(xs), (st.stdev(xs) if len(xs) > 1 else 0.0)


def lc(o, s):
    return math.sqrt(st.mean(o) * st.mean(s))


MODES = ("super", "perf", "eff")

print("== random50 (each run = median of 63 reps)")
print(f"{'core':6} {'b cyc/iter':>14} {'bc/b orig':>14} {'bc/b swapped':>14} {'cancelled':>9} runs")
for mode in MODES:
    o = [r["data-random50"] for r in runs[(mode, "random50", False)] if "data-random50" in r]
    s = [r["data-random50"] for r in runs[(mode, "random50", True)] if "data-random50" in r]
    if not o or not s:
        continue
    b = ms([x[0] for x in o + s])
    ro, rs = [x[1] / x[0] for x in o], [x[1] / x[0] for x in s]
    print(f"{mode:6} {b[0]:8.3f}±{b[1]:5.3f} {ms(ro)[0]:7.3f}±{ms(ro)[1]:5.3f} "
          f"{ms(rs)[0]:7.3f}±{ms(rs)[1]:5.3f} {lc(ro, rs):9.3f} {len(o)}+{len(s)}")

print("\n== taken-branch knee: b cyc/branch, bc/b orig | swapped | cancelled")
sizes = sorted({int(k[2:]) for v in runs.values() for r in v for k in r if k.startswith("tk")})
print(f"{'N':>5} " + " ".join(f"{m:>30}" for m in MODES))
for n in sizes:
    k, cells = f"tk{n}", []
    for mode in MODES:
        o = [r[k] for r in runs[(mode, "knee", False)] if k in r]
        s = [r[k] for r in runs[(mode, "knee", True)] if k in r]
        if not o or not s:
            cells.append(f"{'-':>30}")
            continue
        b = st.mean(x[0] for x in o + s) / n
        ro, rs = [x[1] / x[0] for x in o], [x[1] / x[0] for x in s]
        cells.append(f"{b:6.3f} {st.mean(ro):6.3f}|{st.mean(rs):6.3f}|{lc(ro, rs):6.3f}")
    print(f"{n:5} " + " ".join(cells))

print("\n== pollution: B cost (cyc/group) with b filler | bc filler; bc-b delta orig, swapped")
pk = sorted({k for v in runs.values() for r in v for k in r if k.startswith("pol") and not k.startswith("polA")},
            key=lambda k: tuple(map(int, k[3:].split("_"))))
for mode in MODES:
    print(f"-- {mode}")
    for k in pk:
        g, fl = map(int, k[3:].split("_"))
        ka = "polA" + k[3:]
        d = {}
        for sw in (False, True):
            rows = [r for r in runs[(mode, "pollution", sw)] if k in r and ka in r]
            d[sw] = [((r[k][0] - r[ka][0]) / g, (r[k][1] - r[ka][1]) / g, r[k][1] / r[k][0]) for r in rows]
        if not d[False] or not d[True]:
            continue
        allr = d[False] + d[True]
        bb, bbc = st.mean(x[0] for x in allr), st.mean(x[1] for x in allr)
        do, ds = ms([x[1] - x[0] for x in d[False]]), ms([x[1] - x[0] for x in d[True]])
        acost = st.mean(r[ka][0] for sw in (False, True) for r in runs[(mode, "pollution", sw)] if ka in r) / g
        print(f"  {k:12} A+filler {acost:7.2f}  B: {bb:6.2f} | {bbc:6.2f}   delta {do[0]:+6.2f}±{do[1]:4.2f}, "
              f"{ds[0]:+6.2f}±{ds[1]:4.2f}   pol bc/b {lc([x[2] for x in d[False]], [x[2] for x in d[True]]):.3f}")
