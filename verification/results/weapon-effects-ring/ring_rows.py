#!/usr/bin/env python3
"""The 1024-record ring's rows (docs/verification/weapon-effects.md, "Ring 1024 and the reach cull", 2026-11-07) from the native fixture
logs (default verification/probe/build/weapon-effects/fixture.log and build/verification/weapon-effects-hook/fixture.log):
the hook fixture's OVERFLOW and EVENT_COST rows, the stage and hull CPU rows and the gates' verdicts. The sizes come
from ring_sizes.py beside this script. Usage: ring_rows.py [fixture.log ...] > ring_rows_out.txt"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KEYS = ('OVERFLOW', 'EVENT_COST', 'STAGE_CPU', 'HULL_CPU', 'CLASS_CPU', 'STACK_DEPTH', 'RESULT', 'CHECK pass overflow',
        'CHECK FAIL overflow', 'CHECK pass event_cost', 'CHECK FAIL event_cost', 'CHECK pass hull_cpu', 'CHECK FAIL hull_cpu',
        'CHECK pass hull_ring', 'CHECK FAIL hull_ring', 'CHECK pass stage_cpu', 'CHECK FAIL stage_cpu',
        # the follow-ups: visible-first owners, the out-of-reach cull's edge sweep
        'HULL case=visible_first', 'HULL case=reach ', 'HULL case=reach_summary', 'CHECK hull_reach', 'CHECK hull_visible',
        # the review fixes: the moving owner's spark sphere, the owners past the caps through the production reader
        'HULL case=reach_moving', 'STAGE_OWNERS', 'CHECK stage_owners')
DEFAULT = [ROOT / 'build/verification/weapon-effects-hook/fixture.log',
           ROOT / 'verification/probe/build/weapon-effects/fixture.log']
for path in sys.argv[1:] or DEFAULT:
    if not Path(path).exists():
        continue
    print(f'# {Path(path).relative_to(ROOT) if Path(path).is_relative_to(ROOT) else path}')
    for line in Path(path).read_text(errors='replace').splitlines():
        if line.startswith(KEYS):
            print(line)
