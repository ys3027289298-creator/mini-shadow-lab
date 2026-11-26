CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: transmit

transmit: test/transmit_selftest.c src/transmit.c src/transmit.h
	$(CC) $(CFLAGS) -I src $< src/transmit.c -o $@ -lm

test: transmit
	./transmit

clean:
	rm -f transmit

.PHONY: all test clean
