#!/usr/bin/env python3
"""Refusal-class evidence for uv2 / material_outside_table / dominant_slot_missing (read-only, bottle X3 archives via
lod_overlay.original_assets + bob1). Usage: python3 verification/results/lod-overlay-batch/classes/classes.py > classes_out.txt"""
import re, shlex, sys
from collections import Counter
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1, lod_overlay, lod_atlas, body_materials  # noqa

rows = []
for line in open(ROOT / 'verification/results/lod-overlay-batch/census.txt'):
    t = shlex.split(line)
    if not t: continue
    d = dict(x.split('=', 1) for x in t[1:] if '=' in x); d['name'] = t[0]
    d['refuse'] = d.get('refuse', '-').split(','); d['r0_drawn'] = int(d.get('r0_drawn', 0) or 0)
    rows.append(d)
assets, _ = lod_overlay.original_assets(bob1.DEFAULT_GAME)
by = {bob1.body_stem(v[-1]['path'])[len('objects/'):]: k for k, v in assets.entries.items()
      if v[-1]['path'].lower().endswith('.pbb')}

def load(name):
    tree = bob1.parse(assets.read_entry(assets.entries[by[name]][-1])); assets.cache.clear()
    return tree, bob1.lods(tree)[0], bob1.materials(tree)

def cls(c): return [r for r in rows if c in r['refuse']]

def split(rs, label):
    print(f'## {label}: n={len(rs)} cat={dict(Counter(r["cat"] for r in rs))}')
    print('  r0_drawn bands', dict(Counter('1' if r['r0_drawn'] <= 1 else '2-9' if r['r0_drawn'] < 10 else '>=10' for r in rs)))
    print('  r0_drawn bands ship/station', dict(Counter('1' if r['r0_drawn'] <= 1 else '2-9' if r['r0_drawn'] < 10 else '>=10'
                                                       for r in rs if r['cat'] in ('ship', 'station'))))
    return sorted(rs, key=lambda r: -r['r0_drawn'])

def pv(m, key):
    for n, ty, v in m.get('params', []):
        if n.decode('latin1').lower() == key.lower(): return v.decode('latin1') if ty == 8 else tuple(v)
    return None

def base(s): return None if s is None else re.split(r'[\\/]', s)[-1]

# 1 uv2
top = split(cls('uv2'), 'uv2')
for r in top[:15]: print(f'  top {r["name"]} cat={r["cat"]} r0_drawn={r["r0_drawn"]} uv2={r["uv2"]} pts={r["r0_points"]}')
for r in [r for r in top if r['cat'] in ('ship', 'station')][:5]:
    tree, r0, mats = load(r['name'])
    idx = {i for i, p in enumerate(r0['points']) if p[0] & 4}
    same = near = inunit = 0; u1r = [1e9, -1e9]
    for i in idx:
        p = r0['points'][i]; o = 1 + (3 if p[0] & 1 else 0); u1, v1, u2, v2 = p[o:o + 4]
        same += (u1 == u2 and v1 == v2); near += abs(u1 - u2) <= 1 and abs(v1 - v2) <= 1
        inunit += 0 <= u2 <= 65536 and 0 <= v2 <= 65536
        u1r = [min(u1r[0], u1 / 65536), max(u1r[1], u1 / 65536)]
    mu = Counter(); nog = 0
    for p in r0['parts']:
        for g in p['groups']:
            if any(a in idx or b in idx or c in idx for a, b, c, _ in g['faces']): mu[g['material']] += 1
            else: nog += 1
    print(f'  uv2body {r["name"]}: uv2pts={len(idx)}/{len(r0["points"])} identical={same} within1/65536={near} '
          f'share={near/len(idx):.3f} uv2_in_[0,1]={inunit/len(idx):.3f} uv1_u_range=[{u1r[0]:.1f},{u1r[1]:.1f}] '
          f'groups_touching_uv2={sum(mu.values())} groups_not={nog}')
    ms = [mats[i] for i in mu if 0 <= i < len(mats)]
    print('    effects', dict(Counter(m.get('effect', b'classic').decode('latin1') for m in ms)))
    for k in ('t_OcclusionTexture', 't_LightMapTexture', 't_BumpTexture', 't_DetailTexture', 'Tex2', 'g_MatOcclStr'):
        print(f'    {k}', dict(Counter(str(base(pv(m, k)) if isinstance(pv(m, k), str) else pv(m, k)) for m in ms).most_common(4)))

# 2 material_outside_table
top = split(cls('material_outside_table'), 'material_outside_table')
for r in top[:10]: print(f'  top {r["name"]} cat={r["cat"]} r0_drawn={r["r0_drawn"]} mat={r["mat"]}')
allidx = Counter(); per = Counter(); neg = pos = 0
for r in top:
    tree, r0, mats = load(r['name'])
    bad = [g['material'] for p in r0['parts'] for g in p['groups'] if not 0 <= g['material'] < len(mats)]
    per[len(bad)] += 1; neg += sum(b < 0 for b in bad); pos += sum(b >= 0 for b in bad)
    for b in bad: allidx[b] += 1
print(f'  bad groups per body {dict(per)}; negative={neg} nonneg_out_of_range={pos}; values {dict(allidx.most_common())}')
pick = ([r for r in top if r['cat'] in ('ship', 'station')] + [r for r in top if r['cat'] not in ('ship', 'station')])[:6]
for r in pick:
    tree, r0, mats = load(r['name'])
    bad = [(g['material'], hex(p['flags']), len(g['faces'])) for p in r0['parts'] for g in p['groups'] if not 0 <= g['material'] < len(mats)]
    print(f'  ex {r["name"]} cat={r["cat"]} mats={len(mats)} groups={sum(len(p["groups"]) for p in r0["parts"])} '
          f'bad_groups={len(bad)} (index, part flags, faces)={bad[:6]}')

# 3 dominant_slot_missing
top = split(cls('dominant_slot_missing'), 'dominant_slot_missing')
print('  errors', dict(Counter(re.sub(r'material \d+', 'material N', r.get('error', '')) for r in top)))
for r in top[:10]: print(f'  top {r["name"]} cat={r["cat"]} r0_drawn={r["r0_drawn"]}')
sts = [r for r in top if r['cat'] == 'station']
for r in top[:2] + sts[:2]:
    tree, r0, mats = load(r['name'])
    alpha = lod_overlay.alpha_materials(mats); of = Counter()
    for p in r0['parts']:
        if p['flags'] & lod_atlas.HIDDEN_PART: continue
        for g in p['groups']:
            if g['material'] not in alpha: of[g['material']] += len(g['faces'])
    dom = of.most_common(1)[0][0]
    print(f'  ex {r["name"]} cat={r["cat"]} dominant={dom} effect={mats[dom].get("effect", b"").decode("latin1")} slots={list(body_materials.slots(mats[dom]))}')
    print('    opaque (mat, faces, effect, has_light)', [(mi, n, mats[mi].get('effect', b'').decode('latin1'),
          'light' in body_materials.slots(mats[mi])) for mi, n in of.most_common(6)])
