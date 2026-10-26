#!/usr/bin/env python3
"""Summarize a pmcrun JSON-lines log per SPEC benchmark.

Aggregates all invocations of a benchmark (e.g. gcc's multiple inputs) and
prints instructions, cycles, IPC, effective GHz, P-core share, energy and
peak footprint. rusage times are in mach ticks (24 MHz on Apple silicon).
"""
import json
import re
import sys
from collections import OrderedDict

TICK_S = 1 / 24e6

def main(path):
    agg = OrderedDict()
    for line in open(path):
        r = json.loads(line)
        if re.match(r'\S*/bin/spec\w+ ', r['cmd']):   # harness setup (specxz etc.)
            continue
        m = re.search(r'/CPU/([^/]+)/run/', r['cwd'])
        bench = m.group(1) if m else r['cwd']
        a = agg.setdefault(bench, dict(n=0, wall=0, instr=0, cyc=0, pinstr=0, pcyc=0,
                                       user=0, energy=0, peak=0, bad=0))
        a['n'] += 1
        a['wall'] += r['wall']
        a['bad'] += r['status'] != 0
        a['instr'] += r.get('ru_instr', 0)
        a['cyc'] += r.get('ru_cycles', 0)
        a['pinstr'] += r.get('ru_pinstr', 0)
        a['pcyc'] += r.get('ru_pcycles', 0)
        a['user'] += r.get('ru_user_ns', 0) * TICK_S
        a['energy'] += r.get('ru_energy_nj', 0) / 1e9
        a['peak'] = max(a['peak'], r.get('ru_phys_footprint_peak', 0))

    hdr = f"{'benchmark':18}{'inv':>4}{'wall s':>8}{'instr (G)':>11}{'cycles (G)':>11}{'IPC':>6}{'GHz':>6}{'P-cyc%':>7}{'J':>7}{'W':>6}{'peak MB':>8}"
    print(hdr)
    print('-' * len(hdr))
    for b, a in agg.items():
        if not a['cyc']:
            continue
        print(f"{b:18}{a['n']:4}{a['wall']:8.1f}{a['instr']/1e9:11.1f}{a['cyc']/1e9:11.1f}"
              f"{a['instr']/a['cyc']:6.2f}{a['cyc']/a['user']/1e9 if a['user'] else 0:6.2f}"
              f"{100*a['pcyc']/a['cyc']:7.1f}{a['energy']:7.0f}{a['energy']/a['wall']:6.1f}"
              f"{a['peak']/2**20:8.0f}{'  FAILED' if a['bad'] else ''}")

if __name__ == '__main__':
    main(sys.argv[1])
