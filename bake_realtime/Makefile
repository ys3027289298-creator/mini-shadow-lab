CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: bake_realtime

bake_realtime: src/bake_realtime.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: bake_realtime
	./bake_realtime

clean:
	rm -f bake_realtime
