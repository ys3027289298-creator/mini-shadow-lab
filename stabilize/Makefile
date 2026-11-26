CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: stabilize

stabilize: src/stabilize.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: stabilize
	./stabilize

clean:
	rm -f stabilize
