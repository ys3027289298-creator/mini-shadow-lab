CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: sampling

sampling: src/sampling.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: sampling
	./sampling

clean:
	rm -f sampling

.PHONY: all test clean
