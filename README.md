# Apple silicon PMU tools

Small tools for measuring CPU behaviour on Apple silicon Macs, written while running
SPEC CPU2026 on an Apple M6 Mac mini (Mac18,5) under macOS 27. They also contain a
microbenchmark study of FEAT_HBC (`BC.cond`, the "branch consistent" hint).

**Status of the PMU on macOS 27:** the configurable performance counters are not
usable. `kpc_set_config` returns `EPERM` on macOS 27.0 and 27.0.1, even as root and even
from Instruments. Everything here therefore relies on the counters the kernel exposes for
each process through `proc_pid_rusage(RUSAGE_INFO_V6)`: exact instructions and cycles,
plus Super-core (perflevel0) instructions and cycles, CPU time and an energy estimate.

## Tools

| File | What it does |
|---|---|
| `pmcrun.c` | Runs a command and appends one JSON line of its rusage counters to a log. Designed as a SPEC `submit` wrapper (see below). Also has `setup` / `teardown` / `list` modes for the kperf configurable counters, which fail with `EPERM` on macOS 27. |
| `ipctest.c` | IPC calibration: dependent `eor` / `add` / `mul` / `fadd` chains (expect IPC ≈ 1 or 1/latency), a random-cycle L1 pointer chase, and 8-way independent chains. |
| `smcread.c` | AppleSMC reader: `list`, read one key, or `sample MS KEY…` to CSV (wildcards like `'Tp0*'` work). On the M6, `PSTR` behaves like whole-machine DC power, `PPMR` like the SoC rail and `PHPC` like the CPU complex; these meanings are inferred from how they respond to load. |
| `hogfix.c` | Fixed-priority (non-timeshare, user-interactive) spinner thread. Running N of these with `sudo` occupies the fastest cores, which steers a job onto slower core types (see below). |
| `coretype-runs.sh` | Runs a SPEC config on Super, Performance or Efficiency cores using `hogfix`, with `smcread` sampling and `pmcrun` logging. |
| `pmcsummary.py` | Per-benchmark summary of a `pmcrun` log (instructions, cycles, IPC, GHz, Super-core share). |
| `pmcpower.py` | Joins a `pmcrun` log with an `smcread` CSV per benchmark (average power, energy, temperature). |
| `make-comparison-csv.py` | Builds the compiler / core-type comparison CSV from SPEC results plus these logs. Set `SPEC_RESULT` to the SPEC `result/` directory. |
| `hbcbench/` | FEAT_HBC (`BC.cond`) microbenchmarks and the writeup (`hbc-writeup.pdf`). |

## Build

```sh
make            # pmcrun, ipctest, smcread, hogfix with the system compiler
```

`hbcbench/Makefile` assembles `BC.cond`, so it needs a clang that accepts
`-mcpu=apple-m5` (or any CPU with `+hbc`). It defaults to
`$HOME/opt/llvm-main/bin/clang`; override it with `make CC=/path/to/clang`.

## Using pmcrun with SPEC CPU

Add to a config:

```
%ifdef %{pmclog}
submit = /path/to/pmcrun run -o %{pmclog} -- ${command}
%endif
```

then run with `runcpu --define pmclog=/abs/path/log.jsonl ...`. Each benchmark
invocation produces one line containing `ru_instr`, `ru_cycles`, `ru_pinstr`,
`ru_pcycles` (Super cores only), `ru_user_ns`, `ru_sys_ns`, `ru_user_pns`,
`ru_energy_nj`, `ru_phys_footprint_peak`, wall time and exit status.

How the counts are taken: `pmcrun` waits with `waitid(..., WEXITED | WNOWAIT)`, which
leaves the finished child unreaped, then reads its `proc_pid_rusage`. The counts are
therefore exact whole-process totals. They cover only the launched process, not its
children, and include its kernel-mode time. `ru_energy_nj` is the kernel's estimate, not
a measurement.

Useful derived values: IPC = `ru_instr / ru_cycles`; clock = `ru_cycles / ru_user_ns`;
Super-core share = `ru_pcycles / ru_cycles`.

Long runs: macOS sleeps the system under `pmset sleep 1` even while a benchmark runs.
Wrap runs in `caffeinate -i`.

## Steering work onto a core type

macOS has no CPU-affinity API. On the M6, CPUs 6–7 are Super cores, 8–11 Performance
cores, and 0–5 Efficiency cores. Occupying the faster cores with `hogfix` spinners moves
an ordinary (default-QoS) job down:

| Spinners (`sudo hogfix SECONDS &`) | Job lands on |
|---|---|
| 0 | Super |
| 2 | Performance |
| 6 | Efficiency |

Always verify placement from `ru_pcycles / ru_cycles` (about 100% on Super cores, 0%
elsewhere) and the clock rate (about 4.6–4.8 GHz Super, 4.5 Performance, 3.0–3.4
Efficiency). `taskpolicy -b` and `-c maintenance` also force Efficiency cores, but clamp
them to about 1.1 GHz. Watch out for BSD `seq 0`, which prints `1 0`, not nothing; that
starts two spinners where you asked for none.

## FEAT_HBC study (`hbcbench/`)

The M6 reports `hw.optional.arm.FEAT_HBC = 1`, but neither Apple clang nor upstream LLVM
emits `BC.cond`. The benchmarks compare `B.cond` and `BC.cond` kernels that are
otherwise identical: hot loops, large-footprint and cold code, a taken-branch capacity
sweep, and a predictor-pollution test. Each comparison is run on all three core types.
Results: no measurable effect anywhere. Every apparent effect (up to 47%) followed code
placement and disappeared under a randomized multi-placement control. Details, method
and tables are in `hbcbench/hbc-writeup.pdf`; the raw outputs are the `*.out` files.

| Generator / driver | Experiment |
|---|---|
| `gen_hbc.py`, `hbcbench.c` | Hot-loop kernels (single, never-taken, always-taken, alternating, period-8, data-dependent) |
| `gen_cold.py`, `coldbench.c` | Large-footprint (to 4 MiB) and cold (L2- and predictor-flushed) code |
| `gen_tab.py`, `tabbench.c` | Taken-branch knee sweep and predictor-pollution test |
| `gen_layout.py`, `layoutbench.c` | Multi-placement control: many copies of each variant at random offsets |

The generated assembly is not checked in (some files exceed 100 MB); the Makefile and
`build-main.sh` regenerate it.

## License

BSD 3-Clause; see `LICENSE`.
