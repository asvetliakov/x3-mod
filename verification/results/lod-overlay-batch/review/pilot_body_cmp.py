"""Body-member identity of the batch path vs the Run 71 pilot (addon/05 legacy marker overlay_decoded_sha256),
without baking: lod_atlas bake/encode/check stubbed (atlas bytes NOT compared). Read-only on the bottle."""
import sys, json, hashlib, gzip
from pathlib import Path
R = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(R / 'tools/analysis'), str(R / 'verification/probe')]
import bob1, lod_overlay, lod_atlas, lod_batch_census as census
from inspect_x3 import read_catalogue
game = Path(bob1.DEFAULT_GAME)
marker = json.loads((game / 'addon/05.x3m-lod.json').read_text())
want = {b['name'].lower(): b for b in marker['bodies']}
lod_atlas.bake = lambda layout, textures: {}
lod_atlas.encode = lambda images, layout, fmt: {s: dict(dds=b'DDS stub', bytes=8, sha256='0', error=[], decoded=None, format='DXT1', levels=1)
                                                for s in layout['slots']} if 'slots' in layout else {}
lod_atlas.check = lambda *a, **k: dict(faces=0, vertices=0, inside=0, in_gutter=0, max_map_error_texels=0.0, sampled_faces=0, slots={})
_collapse = lod_atlas.collapse
def collapse(*a, **k):
    res = _collapse(*a, **k); res['layout']['slots'] = res['slots']; return res
lod_atlas.collapse = collapse
names = ['ships/argon/argon_TL', 'ships/argon/argon_M2', 'ships/argon/argon_M1', 'Stations/others/military_outpost_middleb']
plain = lambda member: Path(member.replace('\\', '/')).stem
qual = lod_overlay.qualified_stem
opts = dict(sizes=(1024, 2048), include_other=False, widths=(1280,), rule=dict(census.RULE))
rows, skipped = census.run(game, opts, 1, only={census.body_key(n) for n in names}, include_text=True)
print('skipped sources', skipped, 'rows', [(r['name'], r['t_pad'], r['eligible'], r['refuse']) for r in rows])
atlas_opts = dict(sizes=(1024, 2048), fmt='dxt', specular=True, bump=True, screen_width=1280, min_texels=0.5)
assets, _ = lod_overlay.original_assets(game)
for mode in ('plain-stem', 'qualified'):
    lod_overlay.qualified_stem = plain if mode == 'plain-stem' else qual
    for r in rows:
        res = lod_overlay.bake_body(assets, r, atlas_opts)
        w = want[r['name'].lower()]
        if 'refused' in res:
            print(mode, r['name'], 'REFUSED', res['refused'][:200]); continue
        out = gzip.decompress(res['members'][0][1])
        sha = hashlib.sha256(out).hexdigest()
        if mode == 'qualified':      # map the qualified names back to the pilot's and re-hash
            q = qual(res['member']).encode(); p = plain(res['member']).encode()
            out2 = out.replace(b'x3m_lod_' + q + b'_', b'x3m_lod_' + p + b'_')
            sha2 = hashlib.sha256(out2).hexdigest()
            print(mode, r['name'], 'decoded', len(out), 'sha==pilot', sha == w['overlay_decoded_sha256'],
                  'after name map sha==pilot', sha2 == w['overlay_decoded_sha256'], 'len delta', len(out) - len(out2),
                  'thresholds', res['manifest']['thresholds'], 'pilot', w['thresholds'])
        else:
            print(mode, r['name'], 'decoded', len(out), 'sha==pilot', sha == w['overlay_decoded_sha256'],
                  'thresholds', res['manifest']['thresholds'], 'pilot', w['thresholds'],
                  'atlas members', [m for m, _ in res['members'][1:]])
