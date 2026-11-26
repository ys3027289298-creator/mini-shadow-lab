CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: depthspan

depthspan: src/depthspan.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: depthspan
	./depthspan

clean:
	rm -f depthspan
