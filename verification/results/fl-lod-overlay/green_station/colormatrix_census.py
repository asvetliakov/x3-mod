"""Census of FL overlay atlas bodies whose merged material inherits a per-material colour matrix.
The merged (synth) material is a copy of its dominant source material (lod_atlas), so a dominant with
colormatrix=1 (Brightness/contrast/saturation/hue) colour-grades the whole atlas; absorbed colormatrix
materials lose theirs when the dominant has none.
Usage: X3M_EDITION=fl colormatrix_census.py GAME MARKER.json... (prints one row per affected body + totals)"""
import sys, json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools/analysis'))
import bob1
game = Path(sys.argv[1]); sfc = bob1._archive_modules(); assets = sfc.Assets(game)
def mats_only(data):
    r = bob1.Reader(data); r.expect('BOB1')
    while True:
        t = r.tag()
        if t in ('INFO', 'NAME'): r.cstr()
        elif t in ('VERS', 'SND1'): r.u32()
        elif t in bob1.MATVER: return [bob1.read_material(r, bob1.MATVER[t]) for _ in range(r.count())]
        else: return []
        r.expect('/' + t[:3])
def grade(m):
    p = {(k.decode() if isinstance(k, bytes) else k): v for k, typ, v in m.get('params', [])}
    on = p.get('colormatrix', [0])[0]
    if not on:
        return 0, 0.0, 1.0, 1.0, 1.0
    return on, round(p.get('hue', [0])[0] / 65536, 2), round(p.get('saturation', [65536])[0] / 65536, 3), \
        round(p.get('Brightness', [65536])[0] / 65536, 3), round(p.get('contrast', [65536])[0] / 65536, 3)
tot = dict(bodies=0, atlas=0, dom_cm=0, absorbed_cm_lost=0, unread=0)
for mk in sys.argv[2:]:
    for b in json.load(open(mk))['bodies']:
        tot['bodies'] += 1
        if b.get('collapse') != 'atlas' or not b.get('atlas'): continue
        key = b['member'].lower().rsplit('.', 1)[0]
        es = [e for x in ('.bob', '.bod') for e in assets.entries.get(key + x, []) if e['source'] == b['source'] and e['path'].lower() == b['member'].lower()]
        try:
            mats = {m['index']: m for m in mats_only(assets.read_entry(es[0]))}
        except Exception as ex:
            tot['unread'] += 1; print('UNREAD', b['name'], repr(ex)[:80]); continue
        share = {}
        for t in b['atlas'].get('tiles', []):
            for mi in t['mats']: share[mi] = share.get(mi, 0) + t['share'] / len(t['mats'])
        for s in b.get('synth', []):
            if not s.get('atlas'): continue
            tot['atlas'] += 1
            d = s['dominant']; g = grade(mats[d])
            cm_abs = [a for a in s['absorbed'] if grade(mats[a])[0]]
            cm_share = sum(share.get(a, 0) for a in cm_abs)
            mism = [a for a in s['absorbed'] if grade(mats[a])[1:] != g[1:]]
            mism_share = sum(share.get(a, 0) for a in mism)
            if mism:
                tot['mismatch_bodies'] = tot.get('mismatch_bodies', 0) + 1
                strong = g[0] and (abs(g[2] - 1) > 1 or 0 < g[1] < 360)
                if strong and mism_share > 0.25: tot['strong_dominant_grade_spread'] = tot.get('strong_dominant_grade_spread', 0) + 1
                print('MISMATCH %s %s dom=%d grade=%s mismatch_share=%.3f strong=%d' % (Path(mk).name[:2], b['name'], d, g, mism_share, strong))
            if g[0]:
                tot['dom_cm'] += 1
                print('DOMINANT_GRADED %s %s dominant=%d hue=%.1f sat=%.2f bright=%.3f dom_share=%.3f graded_share=%.3f atlas_mats=%d'
                      % (Path(mk).name[:2], b['name'], d, g[1], g[2], g[3], share.get(d, 0), cm_share, len(s['absorbed'])))
            elif cm_abs:
                tot['absorbed_cm_lost'] += 1
                print('ABSORBED_GRADE_LOST %s %s dominant=%d graded=%s graded_share=%.3f' % (Path(mk).name[:2], b['name'], d, cm_abs, cm_share))
print('TOTAL', tot)
