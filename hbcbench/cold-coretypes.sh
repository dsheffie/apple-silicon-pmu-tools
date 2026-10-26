#!/bin/zsh
# coldbench on Super / Performance / Efficiency cores, original and swapped layout.
cd ${0:A:h}
HOG=${0:A:h}/../hogfix
for mode in super perf eff; do
    case $mode in super) n=0 ;; perf) n=2 ;; eff) n=6 ;; esac
    for ((i = 0; i < n; i++)); do sudo -n $HOG 100000 & done
    sleep 2
    for pass in 1 2 3; do for bin in coldbench coldbench_swapped; do
        echo "=== $mode (hogs=$n) $bin pass $pass"
        HBC_DEFAULT_QOS=1 ./$bin ${1:-15} ${2:-101}
    done; done
    sudo -n pkill -f $HOG
    sleep 5
done
