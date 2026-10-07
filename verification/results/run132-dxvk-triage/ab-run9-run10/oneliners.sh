#!/bin/sh
# A/B run9 (wined3d) vs run10 (DXVK). From ..: for r in 9 10: python3 {pass_state,other_passes,timing}.py $S > ab-run9-run10/<name>_run$r.txt; inflight.py > inflight_run$r.txt
# here: python3 ab.py $S > ab_run$r.txt   (in-flight phase medians, >50/>100 ms halves, dt by draws/issued quartile)
# failure rows: grep -aE '^[a-z_]*(_failed|_refused)[ _]|failed=[1-9]|unavailable=' $S | awk '{print $1}' | sort | uniq -c
# stderr_kinds_run{5,6,9,10}.txt: strip timestamp, grep err|warn|mvk|GStreamer|DXVK, digits->N, sort|uniq -c; diff 6 vs 10 empty, 5 vs 9 only DXVK/mvk lines absent
