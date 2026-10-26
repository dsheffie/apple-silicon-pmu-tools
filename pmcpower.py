#!/usr/bin/env python3
"""Join a pmcrun JSON-lines log with an smcread sample CSV, per SPEC benchmark.

Usage: pmcpower.py PMC_LOG.jsonl SMC_SAMPLES.csv

For each benchmark, SMC samples falling inside any of its invocations are
averaged (and maxed). Columns:
  sys W   whole-machine DC power (SMC PSTR)
  soc W   SoC/package rail (SMC PPMR)
  cpu W   CPU complex (SMC PHPC)
  proc W  CPU energy attributed to the benchmark process (rusage)
  temps   hottest die sensors (SMC TVDP, max over Tp0*)
Key meanings are inferred from load response; Apple does not document them.
"""
import csv
import json
import re
import sys
from collections import OrderedDict


def main(pmc_path, smc_path):
    rows = list(csv.DictReader(open(smc_path)))
    samples = [{k: float(v) for k, v in r.items()} for r in rows]

    bench = OrderedDict()
    for line in open(pmc_path):
        r = json.loads(line)
        if re.match(r'\S*/bin/spec\w+ ', r['cmd']) or 'start' not in r:
            continue
        m = re.search(r'/CPU/([^/]+)/run/', r['cwd'])
        name = m.group(1) if m else r['cwd']
        b = bench.setdefault(name, dict(spans=[], wall=0, instr=0, cyc=0, energy=0))
        b['spans'].append((r['start'], r['end']))
        b['wall'] += r['wall']
        b['instr'] += r.get('ru_instr', 0)
        b['cyc'] += r.get('ru_cycles', 0)
        b['energy'] += r.get('ru_energy_nj', 0) / 1e9

    first = min(s for b in bench.values() for s, _ in b['spans'])
    idle = [s for s in samples if s['time'] < first]

    def stats(sel, key):
        vals = [s[key] for s in sel if s[key] == s[key]]  # drop NaN
        return (sum(vals) / len(vals), max(vals)) if vals else (float('nan'),) * 2

    hdr = (f"{'benchmark':18}{'n':>4}{'IPC':>6}{'sys W':>7}{'pk':>6}{'soc W':>7}{'cpu W':>7}"
           f"{'proc W':>7}{'TVDP C':>8}{'pk':>6}{'Tp0 C':>7}{'pk':>6}")
    print(hdr)
    print('-' * len(hdr))
    if idle:
        print(f"{'(idle before run)':18}{len(idle):4}{'':6}{stats(idle,'PSTR')[0]:7.2f}{'':6}"
              f"{stats(idle,'PPMR')[0]:7.2f}{stats(idle,'PHPC')[0]:7.2f}{'':7}"
              f"{stats(idle,'TVDP')[0]:8.1f}{'':6}{stats(idle,'Tp0*')[0]:7.1f}")
    for name, b in bench.items():
        sel = [s for s in samples if any(a <= s['time'] <= e for a, e in b['spans'])]
        if not sel or not b['cyc']:
            continue
        sys_w, sys_pk = stats(sel, 'PSTR')
        tv, tv_pk = stats(sel, 'TVDP')
        tp, tp_pk = stats(sel, 'Tp0*')
        print(f"{name:18}{len(sel):4}{b['instr']/b['cyc']:6.2f}{sys_w:7.2f}{sys_pk:6.1f}"
              f"{stats(sel,'PPMR')[0]:7.2f}{stats(sel,'PHPC')[0]:7.2f}{b['energy']/b['wall']:7.2f}"
              f"{tv:8.1f}{tv_pk:6.1f}{tp:7.1f}{tp_pk:6.1f}")


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
