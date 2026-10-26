#!/bin/zsh
# Build multi-placement binaries for every hbcbench kernel, two placement seeds.
cd ${0:A:h}
CC=$HOME/opt/llvm-main/bin/clang
mkdir -p mainlay
for k in single fp1024 fp4096 fp16384 fp65536 tk1024 tk4096 tk16384 tk65536 alt p8 data; do
    for seed in 11 23; do
        python3 gen_layout.py $k 32 $seed > mainlay/$k-s$seed.S &&
        $CC -O2 -Wall -mcpu=apple-m5 -o mainlay/$k-s$seed layoutbench.c mainlay/$k-s$seed.S -lm || echo FAIL $k $seed
    done
done
