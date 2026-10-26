#!/bin/zsh
# random50 (many reps), taken-branch knee sweep, and predictor-pollution test
# on Super / Performance / Efficiency cores, original and swapped layouts.
cd ${0:A:h}
HOG=${0:A:h}/../hogfix
POL=$(python3 -c "
import gen_tab as g
print(','.join([f'pol{a}_{b}' for a,b in g.POLLUTION]+[f'polA{a}_{b}' for a,b in g.POLLUTION]))")
for mode in super perf eff; do
    case $mode in super) n=0 ;; perf) n=2 ;; eff) n=6 ;; esac
    for ((i = 0; i < n; i++)); do sudo -n $HOG 100000 & done
    sleep 2
    for pass in 1 2; do for sfx in "" _swapped; do
        echo "=== $mode random50 hbcbench$sfx pass $pass"
        HBC_DEFAULT_QOS=1 ./hbcbench$sfx 63 data-random50
        echo "=== $mode knee tabbench$sfx pass $pass"
        HBC_DEFAULT_QOS=1 ./tabbench$sfx 9 $(python3 -c "import gen_tab as g; print(','.join(f'tk{n}' for n in g.KNEE))")
    done; done
    for pass in 1 2 3; do for sfx in "" _swapped; do
        echo "=== $mode pollution tabbench$sfx pass $pass"
        HBC_DEFAULT_QOS=1 ./tabbench$sfx 9 $POL
    done; done
    sudo -n pkill -f $HOG
    sleep 5
done
echo DONE
