#!/bin/zsh
cd ${0:A:h}
HOG=${0:A:h}/../hogfix
for mode in super eff; do
    case $mode in super) n=0 ;; eff) n=6 ;; esac
    for ((i = 0; i < n; i++)); do sudo -n $HOG 100000 & done
    sleep 2
    for bin in hbcbench hbcbench_swapped; do for r in 1 2 3 4; do HBC_DEFAULT_QOS=1 ./$bin 7 tk1024 | grep '^tk1024' | sed "s/^/$mode $bin /"; done; done
    sudo -n pkill -f $HOG; sleep 5
done
