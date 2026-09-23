"""Atlas-member identity of the batch path vs the installed Run 71 pilot addon/05 (one body, real bake, pilot params:
width 1280, sizes 1024/2048, specular; plain stems as the pilot named them). Read-only on the bottle."""
import sys, json, hashlib, gzip
from pathlib import Path
R = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(R / 'tools/analysis'), str(R / 'verification/probe')]
import bob1, lod_overlay, lod_atlas, lod_batch_census as census
from inspect_x3 import read_catalogue
game = Path(bob1.DEFAULT_GAME)
name = sys.argv[1] if len(sys.argv) > 1 else 'ships/argon/argon_M1'
cat = game / 'addon/05.cat'
inst = {}
with cat.with_suffix('.dat').open('rb') as f:
    for e in read_catalogue(cat):
        f.seek(e['offset']); inst[e['path'].lower()] = bytes(v ^ 0x33 for v in f.read(e['size']))
lod_overlay.qualified_stem = lambda m: Path(m.replace('\\', '/')).stem
opts = dict(sizes=(1024, 2048), include_other=False, widths=(1280,), rule=dict(census.RULE))
rows, _ = census.run(game, opts, 1, only={census.body_key(name)}, include_text=True)
assets, _ = lod_overlay.original_assets(game)
res = lod_overlay.bake_body(assets, rows[0], dict(sizes=(1024, 2048), fmt='dxt', specular=True, bump=True, screen_width=1280, min_texels=0.5))
for m, d in res['members']:
    i = inst.get(m.lower())
    dec = lambda b: gzip.decompress(b) if b[:2] == b'\x1f\x8b' else b
    print(m, 'stored==installed', d == i, 'decoded==installed', i is not None and dec(d) == dec(i),
          'decoded bytes', len(dec(d)), 'differing bytes', None if i is None or len(dec(d)) != len(dec(i)) else sum(a != b for a, b in zip(dec(d), dec(i))))
