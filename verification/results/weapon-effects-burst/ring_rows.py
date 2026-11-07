#!/usr/bin/env python3
"""ring_rows.py <fixture.log> [...]: the centre-ring gates' figures (docs/verification/weapon-effects.md "Fireball drift
law") from a weapon-effects fixture log (verification/probe/build/weapon-effects/fixture.log): per age the hull burst's
worst-hit local prominence and annulus ratio (CLASS case=burst_centre_ring_median), the death body's
(EXPLOSION case=body_centre_ring), the plasma globe's 0.95 L records, the erosion medians and the two gates' verdicts."""
import re
import sys

KEYS = ('case=burst_centre_ring_median', 'case=body_centre_ring ', 'case=plasma_globe ', 'case=burst_erosion_median',
        'case=body tier=fighter_600m', 'case=twenty_hits_peak', 'CHECK hull_burst_no_centre_ring',
        'CHECK death_body_no_centre_ring', 'CHECK hull_plasma_globe', 'CHECK death_body_erodes', 'MUZZLE gate=fireball_measure')
for path in sys.argv[1:]:
    print('#', path)
    for line in open(path, errors='replace'):
        if any(k in line for k in KEYS):
            line = re.sub(r'annulus_lit=\S+ ', '', line.strip())
            print(line)
