CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: follow

follow: src/follow.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: follow
	./follow

clean:
	rm -f follow
