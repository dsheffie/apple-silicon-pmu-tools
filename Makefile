CC     ?= cc
CFLAGS := -O2 -Wall

all: pmcrun ipctest smcread hogfix

pmcrun: pmcrun.c
	$(CC) $(CFLAGS) -o $@ $<

ipctest: ipctest.c
	$(CC) $(CFLAGS) -o $@ $<

smcread: smcread.c
	$(CC) $(CFLAGS) -o $@ $< -framework IOKit -framework CoreFoundation

hogfix: hogfix.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f pmcrun ipctest smcread hogfix

.PHONY: all clean
