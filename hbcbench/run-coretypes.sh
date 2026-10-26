#!/bin/zsh
# Run hbcbench on Super, Performance and Efficiency cores (hog steering, as in
# ../coretype-runs.sh): 0, 2 or 6 fixed-priority hogs occupy the faster cores.
cd ${0:A:h}
HOG=${0:A:h}/../hogfix
for mode in super perf eff; do
    case $mode in super) n=0 ;; perf) n=2 ;; eff) n=6 ;; esac
    for ((i = 0; i < n; i++)); do sudo -n $HOG 100000 & done
    sleep 2
    echo "=== $mode (hogs=$n)"
    HBC_DEFAULT_QOS=1 ./hbcbench ${1:-5}
    sudo -n pkill -f $HOG
    sleep 5
done
