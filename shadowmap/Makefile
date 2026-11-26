CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: shadowmap bias_lab

shadowmap: src/shadowmap.c
	$(CC) $(CFLAGS) $< -o $@ -lm

bias_lab: src/bias_lab.c
	$(CC) $(CFLAGS) $< -o $@ -lm

test: shadowmap bias_lab
	./shadowmap
	./bias_lab

lab: bias_lab
	./bias_lab

clean:
	rm -f shadowmap bias_lab
