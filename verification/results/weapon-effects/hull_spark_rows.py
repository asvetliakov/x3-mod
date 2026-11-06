#!/usr/bin/env python3
"""The hull fixture's spark-visibility, chase-nose, floors and GPU timing rows from a fixture log (default the native
run's verification/probe/build/weapon-effects/fixture.log). Usage: hull_spark_rows.py [fixture.log ...]"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KEYS = ('HULL case=spark_visibility', 'HULL case=chase_nose', 'HULL case=floors_2km', 'HULL_TIMING', 'RESULT',
        'CHECK hull_budget_20_hits_5120', 'CHECK hull_spark_reach_asymptote', 'CHECK hull_streak_min_footprint')
for path in sys.argv[1:] or [ROOT / 'verification/probe/build/weapon-effects/fixture.log']:
    print(f'# {Path(path).name}')
    for line in Path(path).read_text(errors='replace').splitlines():
        if line.startswith(KEYS):
            print(line)
