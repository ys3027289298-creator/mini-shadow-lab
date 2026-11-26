CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: coverage

coverage: src/coverage.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: coverage
	./coverage

clean:
	rm -f coverage
