CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: cascade

cascade: src/cascade.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: cascade
	./cascade

clean:
	rm -f cascade
