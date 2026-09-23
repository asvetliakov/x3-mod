"""Bolt footprint (docs/architecture/bolt-footprint.md, Implementation): does any stock bullet body's per-vertex UV
stream, in the writer's face-expanded order (3 vertices per face, each carrying its point's UV words), repeat with
a period shorter than the body? Such a sub-period would make bolt_footprint_core.h::detect_period split one
instance into several. Reads objects/effects/weapons/bullet_*.pbb from the game catalogues through
tools/analysis/bob1.py (LOD 0, all parts/groups in order); the 16.16 UV words are compared raw, so a common
per-object scale cannot create or destroy a period. No body bytes are written.
Usage: bullet_uv_periods.py [GAME]  (output: bullet_uv_periods_out.txt beside this script)"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
import bob1, inspect_x3  # noqa: E402
from sector_fog_census import unpack  # noqa: E402
MIN_PERIOD, MAX_PERIOD = 24, 234
game = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
rows = []
for cat in sorted(game.glob('*.cat')):
    entries = inspect_x3.read_catalogue(cat)
    with cat.with_suffix('.dat').open('rb') as f:
        for e in entries:
            p = e['path'].replace('\\', '/')
            if not p.lower().startswith('objects/effects/weapons/bullet_') or not p.lower().endswith('.pbb'): continue
            f.seek(e['offset']); tree = bob1.parse(unpack(bytes(v ^ 0x33 for v in f.read(e['size']))))
            lod = bob1.lods(tree)[0]
            points = lod['points']
            def uv(index):
                flags, *fields = points[index]
                if not flags & 2: return None
                base = 3 if flags & 1 else 0
                return (fields[base], fields[base + 1])
            stream = [uv(face[k]) for part in lod['parts'] for g in part['groups'] for face in g['faces'] for k in range(3)]
            n = len(stream)
            has_uv = all(v is not None for v in stream)
            periods = [q for q in range(MIN_PERIOD, min(MAX_PERIOD, n - 1) + 1, 3) if n % q == 0 and all(stream[i] == stream[i - q] for i in range(q, n))]
            distinct = len(set(stream)) if has_uv else 0
            # Geometry of the split: for each sub-period q, the sub-instances' centroids (16.16 units) relative to the
            # body's centroid and their half-extent along the body's longest axis, against the whole body's.
            def pos(index):
                flags, *fields = points[index]
                return fields[0:3] if flags & 1 else None
            pstream = [pos(face[k]) for part in lod['parts'] for g in part['groups'] for face in g['faces'] for k in range(3)]
            geometry = []
            if periods and all(v is not None for v in pstream):
                centroid = [sum(v[a] for v in pstream) / n for a in range(3)]
                spans = [max(v[a] for v in pstream) - min(v[a] for v in pstream) for a in range(3)]
                axis = spans.index(max(spans))
                whole = max(abs(v[axis] - centroid[axis]) for v in pstream)
                for q in periods:
                    parts_ = []
                    for i in range(n // q):
                        seg = pstream[i * q:(i + 1) * q]
                        c = [sum(v[a] for v in seg) / q for a in range(3)]
                        off = max(abs(c[a] - centroid[a]) for a in range(3))
                        half = max(abs(v[axis] - c[axis]) for v in seg)
                        parts_.append((off, half))
                    geometry.append((q, whole, parts_))
            rows.append((p, n, has_uv, distinct, periods, geometry))
for p, n, has_uv, distinct, periods, geometry in rows:
    print(f'{p} vertices {n} uv_words {int(has_uv)} distinct_uv {distinct} sub_periods {",".join(map(str, periods)) or "none"}')
    for q, whole, parts_ in geometry:
        detail = ' '.join(f'off={off / 65536:.4f},half={half / 65536:.4f}' for off, half in parts_)
        print(f'  sub_period {q} whole_half_extent {whole / 65536:.4f} segments {detail}')
print(f'bodies {len(rows)} with_sub_period {sum(1 for r in rows if r[4])} without_uv {sum(1 for r in rows if not r[2])} '
      f'max_segment_centroid_offset {max((off for r in rows for _, _, parts_ in r[5] for off, _ in parts_), default=0) / 65536:.4f}')
