#!/bin/bash
# Reproduce every number and figure in REPORT.md.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p artifacts/csv artifacts/figs

make clean >/dev/null
make

echo "== equivalence + per-stage timing =="
./bench compare 30 2 0 ref nat    A_r2  | tee artifacts/csv/final_A_r2.txt
./bench compare 30 1 0 ref nat    A_r1  | tee artifacts/csv/final_A_r1.txt
./bench compare 30 0 0 ref nat    A_r0  | tee artifacts/csv/final_A_r0.txt
./bench compare 30 2 0 ref fast:0 B_r2  | tee artifacts/csv/final_B_r2.txt

echo "== difference funnels =="
./bench funnel 30 2 0 ref nat    | tee artifacts/csv/funnel_A.txt
./bench funnel 30 2 0 ref fast:0 | tee artifacts/csv/funnel_B.txt

echo "== margin band + error-amplification causal experiment =="
./margin 30 2 0 ref nat    | tee artifacts/csv/margin_pairA.txt
./margin 30 2 0 ref fast:0 | tee artifacts/csv/margin_pairB.txt

echo "-> controlled divergence trigger (unit level)"
./unit_trigger | tee artifacts/csv/unit_trigger.txt

echo "== sweeps =="
./bench sweep-elev 2 0 nat    > artifacts/csv/sweep_elev_nat.csv
./bench sweep-elev 2 0 fast:0 > artifacts/csv/sweep_elev_fast0.csv
./bench sweep-k 30 2 0        > artifacts/csv/sweep_k_30.csv

echo "== fine grazing scan (3.0 -> 0.05 deg) =="
./bench sweep-elev 2 0 nat 3.0 0.05 0.1 > artifacts/csv/grazing_trigger.csv

echo "== quantized shadow maps (f16=1, u16=2) =="
for mode in 1 2; do
  ./bench compare 30 2 $mode ref nat q${mode} | sed -n '2p' | tee -a artifacts/csv/quantized.txt
done

echo "== figures =="
python3 plot/plot.py
echo "DONE. See REPORT.md, artifacts/csv, artifacts/figs"
