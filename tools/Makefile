CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra

all: seam_analyze

seam_analyze: seam_analyze.c
	$(CC) $(CFLAGS) $< -o $@ -lm

# full text report: spans, texel sizes, seam jumps, back-solved ratios
analyze: seam_analyze
	./seam_analyze analyze

# per-preset CSVs for the trade-off chart (regenerate artifacts/)
csv: seam_analyze
	@for p in near-field mid far; do \
		./seam_analyze sweep $$p > ../artifacts/sweep_$$p.csv; \
		./seam_analyze curve $$p > ../artifacts/curve_$$p.csv; \
	done
	@echo "wrote artifacts/sweep_*.csv and artifacts/curve_*.csv"

# self-contained HTML/SVG report from those CSVs
chart: csv
	python3 -I make_chart.py

clean:
	rm -f seam_analyze

.PHONY: all analyze csv chart clean
