CC     := $(HOME)/opt/llvm-main/bin/clang
CFLAGS := -O2 -Wall -mcpu=apple-m5

hbcbench: hbcbench.c hbcbench.S
	$(CC) $(CFLAGS) -o $@ hbcbench.c hbcbench.S

hbcbench.S: gen_hbc.py
	python3 gen_hbc.py > $@

clean:
	rm -f hbcbench hbcbench.S

coldbench: coldbench.c coldbench.S
	$(CC) $(CFLAGS) -o $@ coldbench.c coldbench.S

coldbench_swapped: coldbench.c coldbench_swapped.S
	$(CC) $(CFLAGS) -o $@ coldbench.c coldbench_swapped.S

coldbench.S: gen_cold.py
	python3 gen_cold.py > $@

coldbench_swapped.S: gen_cold.py
	python3 gen_cold.py --bc-first > $@

tabbench: tabbench.c tabbench.S
	$(CC) $(CFLAGS) -o $@ tabbench.c tabbench.S

tabbench_swapped: tabbench.c tabbench_swapped.S
	$(CC) $(CFLAGS) -o $@ tabbench.c tabbench_swapped.S

tabbench.S: gen_tab.py
	python3 gen_tab.py > $@

tabbench_swapped.S: gen_tab.py
	python3 gen_tab.py --bc-first > $@
