#!/usr/bin/env python3
"""Turn a list of real incidents into the coverage ratio the rule question asks
for.  Fill incidents.csv with one row per incident (see header); this prints
how many the narrow rule and the generalised rule would have caught, plus the
per-layer breakdown.

Columns:
  id,date,class,trigger
where class is one of the four measured defect classes:
  capacity | div | uninit | clear
(or 'other' for out-of-family).  'trigger' is free text for the write-up.

With no CSV, it runs on out/runtime.json to show the method end-to-end.
"""
import csv, os, sys, json
from collections import Counter, defaultdict

RULES = {
    # narrow: only the literal black-frame predicate the incident showed
    "narrow":  {"clear", "capacity"},
    # generalised one level: any degenerate value that reaches the
    # failure-is-black branch (adds div + uninit)
    "general": {"clear", "capacity", "div", "uninit"},
}
LAYERS = {
    # assignment is by 'which environment can actually decide it', validated
    # against a held-out set -- NOT by detector strength.
    "code (compile-time)": lambda c: c == "uninit",
    "review (scenario)":   lambda c: c in ("div", "clear"),
    "runtime assert":      lambda c: True,   # incl. capacity, which source checks miss
}

def report(rows):
    n = len(rows)
    if n == 0:
        print("no rows"); return
    print(f"incidents = {n}")
    print(f"{'class':10s} {'count':>5s} {'share':>6s}")
    for c, k in Counter(r["class"] for r in rows).most_common():
        print(f"{c:10s} {k:>5d} {k/n:>6.0%}")
    print()
    for name, cats in RULES.items():
        hit = sum(1 for r in rows if r["class"] in cats)
        print(f"rule '{name}': catches {hit}/{n} = {hit/n:.0%}")
    print()
    print("by environment:")
    for lname, pred in LAYERS.items():
        hit = sum(1 for r in rows if pred(r["class"]))
        print(f"  {lname:22s} {hit}/{n} = {hit/n:.0%}")

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else None
    if path and os.path.exists(path):
        with open(path) as f:
            rows = list(csv.DictReader(f))
        report(rows)
        return
    # demo on measured corpus
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "runtime.json")
    data = json.load(open(p))
    rows = [{"class": d["dtype"]} for d in data if d["event"]]
    print("(no CSV given -- demonstrating on the measured corpus in out/runtime.json)")
    print("Fill incidents.csv with the last 3 months of failures and re-run.\n")
    report(rows)
    tmpl = os.path.join(os.path.dirname(os.path.abspath(__file__)), "incidents.csv")
    if not os.path.exists(tmpl):
        with open(tmpl, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["id", "date", "class", "trigger"])
            w.writerow(["INC-001", "2026-09-14", "clear",
                        "streamed depth tile left unwritten; cleared to 0; slab fully black"])
            w.writerow(["INC-002", "2026-10-02", "div",
                        "receiver coplanar with light plane; penumbra divided by 0"])
            w.writerow(["...", "", "capacity|div|uninit|clear|other", "..."])
        print(f"\nwrote template {tmpl} -- replace the rows with real incidents.")

if __name__ == "__main__":
    main()
