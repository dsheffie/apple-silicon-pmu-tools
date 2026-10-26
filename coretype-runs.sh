#!/bin/zsh
# Run SPEC intrate rate-1 once per core type on the M6 (2 Super, 4 Performance,
# 6 Efficiency cores; Super+Performance share the "PS" cluster, CPUs 6-11).
#   super: no steering (a lone busy thread lands on a Super core)
#   perf:  2 fixed-priority hogs occupy the Super cores
#   eff:   6 fixed-priority hogs occupy all Super+Performance cores
# Hogs are register-only spinners (no cache/memory traffic) but do add power
# and lower the shared PS-cluster clock, so SMC system power includes them.
# Usage: coretype-runs.sh CONFIG MODE... (run with caffeinate; needs sudo -n)
set -u
CFG=$1; shift
SPEC=${SPEC:-$HOME/cpu2026-release-101-macosx}
HERE=${0:A:h}
HOG=$HERE/hogfix
cd $SPEC && source ./shrc >/dev/null
for mode in "$@"; do
    case $mode in
        super) nhog=0 ;;
        perf)  nhog=2 ;;
        eff)   nhog=6 ;;
        *) echo "unknown mode $mode"; continue ;;
    esac
    P=$SPEC/result/pmc-$CFG-$mode.jsonl
    C=$SPEC/result/smc-$CFG-$mode.csv
    rm -f $P $C
    $HERE/smcread sample 1000 PSTR PPMR PHPC TVDP 'Tp0*' > $C &
    smc=$!
    sleep 20
    for ((i = 0; i < nhog; i++)); do sudo -n $HOG 100000 & done  # not $(seq): BSD "seq 0" prints 1 0
    sleep 2
    echo "$mode: start $(date +%T) hogs=$nhog"
    runcpu -c $CFG --define pmclog=$P --nobuild --iterations 1 --copies 1 --noreportable \
        --nopower --runmode rate --tune base --size refrate intrate > /tmp/coretype-$mode.log 2>&1
    sudo -n pkill -f "$HOG"
    kill $smc
    L=$(ls -t result/CPU2026.*.log | grep -v debug | head -1)
    echo "$mode: done $(date +%T) $L $(grep -E 'SPECrate.*int_base' ${L%.log}.intrate.refrate.txt | awk '{print $NF}')"
    sleep 30
done
echo ALL DONE
