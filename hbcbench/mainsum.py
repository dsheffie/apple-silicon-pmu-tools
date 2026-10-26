#!/usr/bin/env python3
"""Summarize run-main.out: per core and kernel, pool the per-placement bc/b
ratios of both seeds (64 placements) and report the geometric mean with a 95%
confidence interval, plus mean b cycles per iteration. With --tex, print the
LaTeX table rows for hbc-writeup.tex."""
import collections
import math
import re
import statistics as st
import sys

KERNELS = ["single", "fp1024", "fp4096", "fp16384", "fp65536", "tk1024", "tk4096", "tk16384",
           "tk65536", "alt", "p8", "data-random50", "data-biased95"]
MODES = ["super", "perf", "eff"]
ratios, bcyc, place = collections.defaultdict(list), collections.defaultdict(list), {}
key = None
for line in open("run-main.out"):
    m = re.match(r"=== (\w+) (\S+) seed (\d+)", line)
    if m:
        key = (m.group(1), m.group(2))
        continue
    f = line.split()
    if key and len(f) == 4 and f[0].isdigit():
        bcyc[key].append(float(f[1]))
        ratios[key].append(float(f[2]) / float(f[1]))
    if key and line.startswith("placement:"):
        place.setdefault(key[0], []).append(line.split()[3])


def ci(xs):
    lx = [math.log(x) for x in xs]
    m, se = st.mean(lx), st.stdev(lx) / math.sqrt(len(lx))
    return math.exp(m), math.exp(m - 1.998 * se), math.exp(m + 1.998 * se), (m / se if se else 0.0)


tex = "--tex" in sys.argv
if not tex:
    print(f"{'kernel':14} " + " ".join(f"{m:>34}" for m in MODES))
for k in KERNELS:
    cells = []
    for mode in MODES:
        xs = ratios.get((mode, k))
        if not xs or len(xs) < 4:
            cells.append(None)
            continue
        g, lo, hi, t = ci(xs)
        cells.append((g, lo, hi, t, st.mean(bcyc[(mode, k)]), len(xs)))
    if tex:
        name = k.replace("data-", "")
        print(f"{name:14} & " + " & ".join(
            "--" if c is None else f"{c[0]:.3f} [{c[1]:.3f}, {c[2]:.3f}]" for c in cells) + r" \\")
    else:
        print(f"{k:14} " + " ".join(
            f"{'-':>34}" if c is None else
            f"{c[0]:.3f} [{c[1]:.3f},{c[2]:.3f}] t={c[3]:+5.2f} n={c[5]:2d}" for c in cells))
if not tex:
    for mode, ps in place.items():
        print(f"placement {mode}: super-core cycle share {min(ps)}..{max(ps)}")
