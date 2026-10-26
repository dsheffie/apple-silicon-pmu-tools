#!/bin/zsh
cd ${0:A:h}
HOG=${0:A:h}/../hogfix
for mode in super eff; do
    case $mode in super) n=0; f=mix-65536,tk-262144,nt-65536,sparse-4096,mix-4096 ;; eff) n=6; f=tk-524288,mix-524288,tk-262144 ;; esac
    for ((i = 0; i < n; i++)); do sudo -n $HOG 100000 & done
    sleep 2
    for pass in 1 2 3 4 5 6 7 8; do for bin in coldbench coldbench_swapped; do
        echo "=== $mode (hogs=$n) $bin pass $pass"
        HBC_DEFAULT_QOS=1 ./$bin 15 151 $f
    done; done
    sudo -n pkill -f $HOG; sleep 5
done
