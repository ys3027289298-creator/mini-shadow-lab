CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -march=native
# note: -ffast-math deliberately NOT used; the double path and the bit-hack
# rsqrt must be measured at their real, portable costs.

all: bench diag margin unit_trigger

bench: src/bench.c src/pipeline.c src/common.h src/bias.h src/pipeline.h
	$(CC) $(CFLAGS) src/bench.c src/pipeline.c -o $@ -lm

diag: src/diag.c src/pipeline.c src/common.h src/bias.h src/pipeline.h
	$(CC) $(CFLAGS) src/diag.c src/pipeline.c -o $@ -lm

margin: src/margin.c src/pipeline.c src/common.h src/bias.h src/pipeline.h
	$(CC) $(CFLAGS) src/margin.c src/pipeline.c -o $@ -lm

unit_trigger: src/unit_trigger.c src/pipeline.c src/common.h src/bias.h src/pipeline.h
	$(CC) $(CFLAGS) src/unit_trigger.c src/pipeline.c -o $@ -lm

test: bench
	./bench compare 30 2 0 ref fast:2 smoke

clean:
	rm -f bench diag margin unit_trigger
