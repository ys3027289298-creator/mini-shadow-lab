CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: biasdir

biasdir: src/biasdir.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: biasdir
	./biasdir

clean:
	rm -f biasdir
