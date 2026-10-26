#!/bin/zsh
# 16-placement b vs bc comparison for random50 and knee-region tkN kernels.
cd ${0:A:h}
HOG=${0:A:h}/../hogfix
for mode in super perf eff; do
    case $mode in super) n=0 ;; perf) n=2 ;; eff) n=6 ;; esac
    for ((i = 0; i < n; i++)); do sudo -n $HOG 100000 & done
    sleep 2
    echo "=== $mode data"; HBC_DEFAULT_QOS=1 ./layout_data 15 20000000
    for k in 512 1152 2048 2560 3072; do
        echo "=== $mode tk$k"; HBC_DEFAULT_QOS=1 ./layout_tk$k 15 $((40000000 / k))
    done
    sudo -n pkill -f $HOG; sleep 5
done
echo DONE
