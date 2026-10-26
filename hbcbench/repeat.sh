#!/bin/zsh
cd ${0:A:h}
HOG=${0:A:h}/../hogfix
run() {  # mode nhog filter
    for ((i = 0; i < $2; i++)); do sudo -n $HOG 100000 & done
    sleep 2
    for r in 1 2 3 4; do HBC_DEFAULT_QOS=1 ./hbcbench 7 $3 | grep -vE '^kernel' | sed "s/^/$1 /"; done
    sudo -n pkill -f $HOG; sleep 5
}
run super 0 tk1024
run perf 2 data-random50
run eff 6 tk1024
