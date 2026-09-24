"""Slot 06 LOD switch: dump TFactories/TDocks rows (winning archive entry, read-only) whose scene/body names match
the overlay bodies seen in run297/run287, with every field, so the field the engine tests at 0x00441639
(row+0x5c == 0x12) can be identified. usage: type_race.py GAME_ROOT [pattern ...]"""
import sys, re
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'analysis'))
import sector_fog_census as sfc
root = Path(sys.argv[1]); pats = [p.lower() for p in sys.argv[2:]] or ['spp', 'usc', 'terran', 'argon_spacedock', 'tech']
a = sfc.Assets(root)
for t in ('TDocks', 'TFactories'):
    data, meta = a.logical(f'types/{t}', ('.pck', '.txt'))
    text = data.decode('latin-1')
    rows = [l for l in text.splitlines() if l and not l.startswith('/')]
    hdr = rows[0]; body = rows[1:]
    print(f'== {t} {meta["source"]} {meta["member"]} header={hdr!r} rows={len(body)}')
    for i, l in enumerate(body):
        f = l.split(';')
        if any(p in l.lower() for p in pats):
            print(f'  idx={i} nfields={len(f)} ' + ' '.join(f'[{k}]{v}' for k, v in enumerate(f[:24])))
