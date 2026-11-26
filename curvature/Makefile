CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: curvature

curvature: src/curvature.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: curvature
	./curvature

clean:
	rm -f curvature
