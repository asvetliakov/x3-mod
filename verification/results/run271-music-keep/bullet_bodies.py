"""Bolt footprint design (docs/architecture/bolt-footprint.md): derived point/face counts of the stock bullet bodies
(objects/effects/weapons/bullet_*.pbb, 01.cat) through tools/analysis/bob1.py; no body bytes are written.
Usage: bullet_bodies.py [GAME]"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
import bob1, inspect_x3  # noqa: E402
from sector_fog_census import unpack  # noqa: E402
game = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
rows = []
for cat in sorted(game.glob('*.cat')):
    entries = inspect_x3.read_catalogue(cat)
    with cat.with_suffix('.dat').open('rb') as f:
        for e in entries:
            p = e['path'].replace('\\', '/')
            if not p.lower().startswith('objects/effects/weapons/bullet_') or not p.lower().endswith('.pbb'): continue
            f.seek(e['offset']); tree = bob1.parse(unpack(bytes(v ^ 0x33 for v in f.read(e['size']))))
            lod = bob1.lods(tree)[0]; faces = sum(len(g['faces']) for part in lod['parts'] for g in part['groups'])
            rows.append((p, len(lod['points']), faces, faces * 3))
for p, points, faces, verts in rows: print(f'{p} points {points} faces {faces} vertices_per_instance {verts}')
print(f'bodies {len(rows)} faces min {min(r[2] for r in rows)} max {max(r[2] for r in rows)}; observed draw primitives 144,168 (first person) and 792,840 (third person): 168=28x6, 840=28x30, 144=24x6, 792=24x33')
