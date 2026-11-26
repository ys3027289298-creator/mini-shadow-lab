CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: nightspan

nightspan: src/nightspan.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: nightspan
	./nightspan

clean:
	rm -f nightspan
