#!/usr/bin/env python3
"""The hull-impact classes' rows (docs/architecture/hull-impact-classes.md, 2026-11-06) from the native fixture logs
(default verification/probe/build/weapon-effects/fixture.log and verification/probe/build/engine-shimmer/fixture.log):
the CLASS cases, the class CPU and timing rows, the hook fixture's HULL_CLASS rows, the shimmer's WEAPON_RECT rows and
the gates' verdicts. Usage: hull_class_rows.py [fixture.log ...]"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KEYS = ('CLASS', 'HULL_CLASS', 'WEAPON_RECT', 'HULL_SLOTS', 'HULL case=budget', 'HULL case=chase_nose', 'HULL case=variants',
        'HULL case=half_hidden', 'HULL case=reset', 'HULL_CPU', 'HULL_TIMING', 'RESULT', 'CHECK hull_class', 'CHECK hull_scorch',
        'CHECK hull_energy', 'CHECK hull_ballistic', 'CHECK hull_plasma', 'CHECK hull_heavy', 'CHECK hull_beam',
        'CHECK hull_ion', 'CHECK hull_missile', 'CHECK hull_repair', 'CHECK hull_asteroid', 'CHECK hull_compose',
        'CHECK hull_min_frames', 'CHECK hull_budget', 'CHECK hull_cpu', 'CHECK hull_classes', 'CHECK weapon_rect',
        'CHECK pass hull_class', 'CHECK hull_reset', 'CHECK hull_chase')
DEFAULT = [ROOT / 'verification/probe/build/weapon-effects/fixture.log',
           ROOT / 'build/verification/weapon-effects-hook/fixture.log',
           ROOT / 'verification/probe/build/engine-shimmer/fixture.log']
for path in sys.argv[1:] or DEFAULT:
    if not Path(path).exists():
        continue
    print(f'# {Path(path).relative_to(ROOT) if Path(path).is_relative_to(ROOT) else path}')
    for line in Path(path).read_text(errors='replace').splitlines():
        if line.startswith(KEYS):
            print(line)
