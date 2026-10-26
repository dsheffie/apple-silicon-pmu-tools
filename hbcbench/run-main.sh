#!/bin/zsh
# Main-table rerun: every hbcbench kernel, 32 random placements x 2 seeds,
# 15 shuffled rounds, on Super / Performance / Efficiency cores.
cd ${0:A:h}
HOG=${0:A:h}/../hogfix
typeset -A IT
IT=(single 40000000 fp1024 40000 fp4096 10000 fp16384 2500 fp65536 500
    tk1024 20000 tk4096 5000 tk16384 800 tk65536 150 alt 30000000 p8 30000000
    data-random50 8000000 data-biased95 20000000)
for mode in super perf eff; do
    case $mode in super) n=0 ;; perf) n=2 ;; eff) n=6 ;; esac
    for ((i = 0; i < n; i++)); do sudo -n $HOG 100000 & done
    sleep 2
    for k in single fp1024 fp4096 fp16384 fp65536 tk1024 tk4096 tk16384 tk65536 alt p8 data-random50 data-biased95; do
        bin=${k%-*}; [[ $k == data-biased95 ]] && export HBC_BIASED=1 || unset HBC_BIASED
        for seed in 11 23; do
            echo "=== $mode $k seed $seed"
            HBC_DEFAULT_QOS=1 ./mainlay/$bin-s$seed 15 $IT[$k]
        done
    done
    sudo -n pkill -f $HOG; sleep 5
done
echo DONE
