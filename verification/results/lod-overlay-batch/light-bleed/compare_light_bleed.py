#!/usr/bin/env python3
"""Light-atlas bleed guard, before/after over one batch dry run pair (lod_overlay.py --batch --dry-run,
the same --only set; OFF with --light-bleed-max 0, ON with the default): per body the draws of C, atlas
size, min texels/px, flagged tiles, remedy and kept materials, then totals. The per-body guard line
(flagged tiles, repack scale) comes from ON's -bodies.txt.

  python3 compare_light_bleed.py OFF_DIR ON_DIR
"""
import json
import re
import sys
from pathlib import Path


def load(d):
    rec = json.loads((Path(d) / 'x3m-lod-batch.json').read_text())
    return {b['name']: b for b in rec['bodies'] if b.get('eligible')}, rec


def guard_lines(d):
    out, name = {}, None
    for line in (Path(d) / 'x3m-lod-batch-bodies.txt').read_text().splitlines():
        m = re.match(r'^(\S+): ', line)
        if m and not line.startswith(' '):
            name = m.group(1)
        elif name and line.strip().startswith('atlas light_bleed='):
            out[name] = line.strip()
    return out


off, _ = load(sys.argv[1])
on, rec = load(sys.argv[2])
lines = guard_lines(sys.argv[2])
print(f'{"body":64s} draws off->on  size off->on  min texels/px off->on  light_bleed kept remedy')
tot = [0, 0]
flagged = kept_groups = 0
for n in sorted(on, key=str.lower):
    a, b = off.get(n, {}), on[n]
    tot[0] += a.get('draws') or 0
    tot[1] += b.get('draws') or 0
    flagged += b.get('light_bleed', 0)
    kept_groups += b.get('kept_light_bleed_draws', 0)
    print(f'{n:64s} {a.get("draws")}->{b.get("draws")}  {a.get("atlas_size")}->{b.get("atlas_size")}'
          f'  {a.get("ratio", 0):.3f}->{b.get("ratio", 0):.3f}  {b.get("light_bleed")} {len(b.get("kept_light_bleed", []))}'
          f' {b.get("light_bleed_remedy")}')
print(f'bodies {len(on)} (off {len(off)}); bodies flagged {sum(1 for b in on.values() if b.get("light_bleed"))};'
      f' tiles flagged {flagged}; kept groups {kept_groups}; draws below the switch (C, summed) {tot[0]} -> {tot[1]}')
print('guard lines of the flagged bodies:')
for n in sorted(on, key=str.lower):
    if on[n].get('light_bleed'):
        print(f'  {n}: {lines.get(n, "-")}')
