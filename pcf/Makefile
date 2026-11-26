CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: pcf

pcf: src/pcf.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: pcf
	./pcf

clean:
	rm -f pcf
