CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: taa

taa: src/taa.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: taa
	./taa

clean:
	rm -f taa results.csv
