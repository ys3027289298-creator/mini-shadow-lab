CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
ART ?= ../artifacts

all: pcf calibrate

pcf: src/pcf.c src/pcf_core.h
	$(CC) $(CFLAGS) src/pcf.c -o $@ -lm

calibrate: src/calibrate.c src/pcf_core.h
	$(CC) $(CFLAGS) src/calibrate.c -o $@ -lm

test: pcf calibrate
	./pcf
	@mkdir -p $(ART)
	./calibrate $(ART)/penumbra_tiers.csv $(ART)/policy_sweep.csv

plot: test
	python3 tools/plot_policy.py $(ART)/policy_sweep.csv $(ART)/penumbra_tiers.csv $(ART)/pcf_policy.png

clean:
	rm -f pcf calibrate
