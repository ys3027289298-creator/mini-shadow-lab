CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: fog

fog: src/fog.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: fog
	./fog

clean:
	rm -f fog
