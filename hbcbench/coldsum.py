import re, sys, collections, statistics as st
d = collections.defaultdict(list)   # (mode, kernel) -> rows
mode = None
for line in open(sys.argv[1]):
    m = re.match(r'=== (\w+) .* (coldbench\S*) pass', line)
    if m: mode, binn = m.group(1), m.group(2); continue
    f = line.split()
    if mode and len(f) == 13 and f[1].isdigit():
        d[(mode, f[0])].append((binn, float(f[3]), float(f[4]), float(f[7]), float(f[8]), float(f[9]), float(f[10])))
def fmt(xs):
    m = st.mean(xs); s = st.stdev(xs) if len(xs) > 1 else 0
    return f"{m:6.3f}±{s:5.3f}"
print(f"{'mode':5} {'kernel':14} {'warm b':>7} {'cold b':>7} | warm bc/b orig   swapped      | cold pair orig   swapped")
for (mode, k), rows in d.items():
    o = [r for r in rows if r[0] == 'coldbench']; s = [r for r in rows if r[0] != 'coldbench']
    wb = st.mean(r[1] for r in rows); cb = st.mean(r[3] for r in rows)
    print(f"{mode:5} {k:14} {wb:7.3f} {cb:7.2f} | {fmt([r[2]/r[1] for r in o])} {fmt([r[2]/r[1] for r in s])} | {fmt([r[6] for r in o])} {fmt([r[6] for r in s])}")

# Layout-cancelled estimate: geometric mean of orig and swapped ratios (placement
# effects flip between layouts, a real hint effect does not).
import math
print("\nlayout-cancelled bc/b (geomean of orig & swapped means); |dev|>1.5% flagged")
for which, idx in (("warm", None), ("cold", 6)):
    devs = []
    for (mode, k), rows in d.items():
        o = [r for r in rows if r[0] == 'coldbench']; s = [r for r in rows if r[0] != 'coldbench']
        f = (lambda r: r[2] / r[1]) if idx is None else (lambda r: r[6])
        g = math.sqrt(st.mean(map(f, o)) * st.mean(map(f, s)))
        both = (st.mean(map(f, o)) - 1) * (st.mean(map(f, s)) - 1) > 0
        devs.append((g, mode, k, both))
    gs = [x[0] for x in devs]
    print(f"{which}: n={len(gs)} mean {st.mean(gs):.4f} sd {st.stdev(gs):.4f} min {min(gs):.3f} max {max(gs):.3f}")
    for g, mode, k, both in devs:
        if abs(g - 1) > 0.015: print(f"   {mode:5} {k:14} {g:.3f}  same-sign-in-both-layouts={both}")
