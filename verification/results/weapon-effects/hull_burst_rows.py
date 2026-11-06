#!/usr/bin/env python3
"""The hull hit's burst, randomised sparks and heavy ring rows (2026-11-06) and the cockpit eye-sphere rows from a
fixture log (default the native run's verification/probe/build/weapon-effects/fixture.log), with the gate verdicts.
Usage: hull_burst_rows.py [fixture.log ...]"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KEYS = ('HULL_SLOTS', 'HULL case=burst_', 'HULL case=spark_random', 'HULL case=ring_', 'HULL case=compose_no_pop',
        'HULL case=resolve_burst', 'HULL case=budget', 'HULL case=half_hidden', 'HULL case=law ', 'HULL case=floors_2km',
        'HULL case=velocity_first_hit', 'HULL case=variants', 'HULL_CPU', 'HULL_TIMING', 'EYE_SPHERE', 'RESULT',
        'CHECK hull_burst', 'CHECK hull_spark', 'CHECK hull_ring', 'CHECK hull_heavy', 'CHECK hull_compose',
        'CHECK hull_resolve_burst', 'CHECK hull_budget', 'CHECK hull_cpu', 'CHECK hull_fireball', 'CHECK cockpit_eye',
        'CHECK hull_half', 'CHECK hull_streak', 'CHECK hull_recovers', 'CHECK hull_reset', 'CHECK hull_programs')
for path in sys.argv[1:] or [ROOT / 'verification/probe/build/weapon-effects/fixture.log']:
    print(f'# {Path(path).name}')
    for line in Path(path).read_text(errors='replace').splitlines():
        if line.startswith(KEYS):
            print(line)
