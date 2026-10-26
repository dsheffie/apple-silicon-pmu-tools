#!/bin/zsh
# Super-core confirmation: 32 random placements x 2 seeds, knee sizes + controls.
cd ${0:A:h}
for seed in 11 23; do for k in 512 1152 2048 2560 3072 6144; do
    echo "=== super tk$k seed $seed"; ./layout_tk${k}_s$seed 15 $((40000000 / k))
done; done
echo DONE
