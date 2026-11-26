CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: shadowmap

shadowmap: src/shadowmap.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: shadowmap
	./shadowmap

clean:
	rm -f shadowmap
