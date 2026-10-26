#!/usr/bin/env python3
"""Build result/m6-compiler-comparison.csv: one row per (benchmark, run).

Each run pairs a SPEC result (ratio, runtime) with pmcrun counters and, when
present, 1 Hz SMC samples. Edit RUNS to add runs. Rows for the Performance and
Efficiency core-type runs have hogs > 0: their SMC system/SoC/CPU power and
temperatures include the hog spinners, so only the per-process counters
(instructions, cycles, IPC, GHz, process energy) are comparable across modes.
"""
import os
import csv
import json
import re
import sys

RESULT = os.environ.get('SPEC_RESULT', os.path.expanduser('~/cpu2026-release-101-macosx/result'))
OUT = f'{RESULT}/m6-compiler-comparison.csv'

L22 = ('LLVM 22.1.4', 'upstream clang 22.1.4 (llvm.org release)', '-O3 (no -mcpu, no LTO)')
AC = ('Apple clang 21', 'Apple clang 21.0.0 (clang-2100.3.34.2)', '-O3 -flto -mcpu=apple-m6')
L24 = ('LLVM 24 (main)', 'upstream clang 24.0.0git (main @ ba8dc06)',
       '-O3 -flto -fuse-ld=lld -mcpu=apple-m5+cpa+faminmax+fp8+lut+pauth-lr+sme-f8f16+sme-f8f32+sme-lutv2')
OS0, OS1 = 'macOS 27.0 (26A428)', 'macOS 27.0.1 (26A434)'

# compiler, os, core_type, hogs, spec run, iterations, pmc log, smc csv
RUNS = [
    (L22, OS0, 'Super (default)', 0, 'CPU2026.006', 3, 'pmc-intrate-rate1.jsonl', None),
    (AC, OS1, 'Super (default)', 0, 'CPU2026.013', 1, 'pmc-apple-clang-m6-power.jsonl', 'smc-apple-clang-m6-power.csv'),
    (L24, OS1, 'Super (default)', 0, 'CPU2026.020', 1, 'pmc-llvm-main-m6-power.jsonl', 'smc-llvm-main-m6-power.csv'),
    (L24, OS1, 'Super', 0, 'CPU2026.024', 1, 'pmc-m6-llvm-main-super.jsonl', 'smc-m6-llvm-main-super.csv'),
    (L24, OS1, 'Performance', 2, 'CPU2026.022', 1, 'pmc-m6-llvm-main-perf.jsonl', 'smc-m6-llvm-main-perf.csv'),
    (L24, OS1, 'Efficiency', 6, 'CPU2026.023', 1, 'pmc-m6-llvm-main-eff.jsonl', 'smc-m6-llvm-main-eff.csv'),
]

COLS = ['benchmark', 'compiler', 'compiler_version', 'flags', 'os', 'core_type', 'hogs', 'spec_run', 'iterations',
        'spec_runtime_s', 'spec_ratio', 'instructions', 'cycles', 'ipc', 'core_ghz', 'super_core_cycle_pct',
        'process_cpu_energy_j', 'process_cpu_power_w', 'peak_memory_mb',
        'smc_system_power_avg_w', 'smc_system_power_peak_w', 'smc_soc_power_avg_w', 'smc_cpu_complex_power_avg_w',
        'smc_hot_sensor_tvdp_avg_c', 'smc_hot_sensor_tvdp_peak_c', 'smc_cpu_die_tp0_max_avg_c',
        'smc_cpu_die_tp0_max_peak_c', 'smc_samples']


def counters(path):
    d = {}
    for line in open(f'{RESULT}/{path}'):
        r = json.loads(line)
        if re.match(r'\S*/bin/spec\w+ ', r['cmd']):
            continue
        b = re.search(r'/CPU/([^/]+)/run/', r['cwd']).group(1)
        if b.startswith('999'):
            continue
        a = d.setdefault(b, dict(spans=[], wall=0, instr=0, cyc=0, pcyc=0, user=0, energy=0, peak=0))
        a['wall'] += r['wall']
        a['instr'] += r['ru_instr']
        a['cyc'] += r['ru_cycles']
        a['pcyc'] += r['ru_pcycles']
        a['user'] += r['ru_user_ns'] / 24e6  # mach ticks, 24 MHz
        a['energy'] += r['ru_energy_nj'] / 1e9
        a['peak'] = max(a['peak'], r['ru_phys_footprint_peak'])
        if 'start' in r:
            a['spans'].append((r['start'], r['end']))
    return d


def selected(run):
    """SPEC's selected (median) runtime and ratio per benchmark."""
    d = {}
    for line in open(f'{RESULT}/{run}.intrate.refrate.txt'):
        m = re.match(r'^(7\d\d\.\w+)\s+1\s+([\d.]+)\s+([\d.]+)\s+\*', line)
        if m:
            d[m[1]] = (float(m[2]), float(m[3]))
    return d


def smc_stats(path, spans):
    rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(open(f'{RESULT}/{path}'))]
    sel = [s for s in rows if any(a <= s['time'] <= e for a, e in spans)]
    if not sel:
        return [''] * 9

    def st(k):
        v = [s[k] for s in sel if s[k] == s[k]]
        return round(sum(v) / len(v), 2), round(max(v), 2)
    return [st('PSTR')[0], st('PSTR')[1], st('PPMR')[0], st('PHPC')[0],
            st('TVDP')[0], st('TVDP')[1], st('Tp0*')[0], st('Tp0*')[1], len(sel)]


def main():
    with open(OUT, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(COLS)
        n = 0
        for (name, ver, flags), os_, core, hogs, run, iters, pmc, smc in RUNS:
            sel, cnt = selected(run), counters(pmc)
            for b, (rt, ratio) in sel.items():
                c = cnt[b]
                row = [b, name, ver, flags, os_, core, hogs, run, iters, rt, ratio, c['instr'], c['cyc'],
                       round(c['instr'] / c['cyc'], 3), round(c['cyc'] / c['user'] / 1e9, 3),
                       round(100 * c['pcyc'] / c['cyc'], 2), round(c['energy'], 1),
                       round(c['energy'] / c['wall'], 3), round(c['peak'] / 2**20, 1)]
                row += smc_stats(smc, c['spans']) if smc and c['spans'] else [''] * 9
                w.writerow(row)
                n += 1
    print(f'wrote {n} rows to {OUT}', file=sys.stderr)


if __name__ == '__main__':
    main()
