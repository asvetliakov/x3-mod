#!/bin/bash
# Run 364 (Mayhem 3, 0.7.0, 5120x1440, x3m.ini: emission_source_gain 1, additive 0, bloom 0, ev_max 0) engine-exhaust triage.
cd "$(dirname "$0")"; R=/tmp/x3-bottleX3-run364; L=$(ls $R/session-*.log)
python3 log_draws.py $L 4490 > log_draws_4490_out.txt
grep "^hull_emission_draw device=1 frame=4490 " $L | python3 -c "
import sys,re
for l in sys.stdin:
    d=dict(re.findall(r'(\w+)=(\S+)',l)); print(d['index'],d['program'],d['ps'][:8],d['gain'],d['primitives'],d['origin_px'],d['origin_w'])" > hull_emission_draws_4490_out.txt
python3 -W ignore exhaust_stats.py $R 4490 4497 2000 350 2800 800 > exhaust_stats_out.txt
bash stock_scan.sh > stock_scan_out.txt
python3 stock_max.py > stock_max_out.txt
python3 -W ignore blend_arith.py > blend_arith_out.txt
