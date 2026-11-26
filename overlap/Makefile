CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: overlap

overlap: src/overlap.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: overlap
	./overlap

clean:
	rm -f overlap
