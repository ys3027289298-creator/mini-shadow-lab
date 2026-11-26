#!/bin/bash
for d in shadowmap cascade pcf bake_realtime; do
  cd "/w/$d" || continue
  make clean >/dev/null 2>&1
  if make >/tmp/$d.log 2>&1; then
    echo "[$d] OK"
    ./$d | tail -2
  else
    echo "[$d] FAIL"
    tail -8 /tmp/$d.log
  fi
done