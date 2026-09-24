import sys, math
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import bob1, lod_overlay, body_materials, lod_atlas
game = Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets, skipped = lod_overlay.original_assets(game)
print('skipped sources', skipped)
entry = bob1.resolve_body(assets, 'stations/x3tc/terran_spp_panel')
print('entry', entry['source'], entry['path'])
data = assets.read_entry(entry)
tree = bob1.parse(data, lod_overlay.MAX_TRAILING)
ladder = bob1.lods(tree); mats = bob1.materials(tree)
print('ladder', [(l['value'], len(l['points']), sum(len(g['faces']) for p in l['parts'] for g in p['groups'])) for l in ladder])
lod0 = ladder[0]
alpha = lod_overlay.alpha_materials(mats, assets, record=lod0)
flagged = {i for i,m in enumerate(mats) if lod_overlay.alpha_flagged(m)}
used = sorted({g['material'] for p in lod0['parts'] for g in p['groups']})
KEYS = (b'g_alphatestenable', b'g_alphablendenable', b'g_alphavalue', b'g_zwriteenable', b'g_srcblend', b'g_destblend', b'g_alpharef', b'g_alphafunc', b'g_enableglow', b'g_cullmode', b'g_alphatestref')
for i in used:
    m = mats[i]
    p = {name.lower(): (typ, val) for name, typ, val in m.get('params', ())}
    sel = {k.decode(): v[1][0] if v[1] else None for k, v in p.items() if k in KEYS or b'alpha' in k or b'zwrite' in k or b'cull' in k}
    print(f'mat {i} effect={lod_atlas.effect_name(m)} slots={body_materials.slots(m)} flagged={i in flagged} alpha={i in alpha} {sel}')
for pi, part in enumerate(lod0['parts']):
    print(f'part {pi} flags={part["flags"]:#x} groups=', [(g['material'], len(g['faces'])) for g in part['groups']])
pts = lod0['points']
print('point flags set', {p[0] for p in pts})
mx = max(math.sqrt(p[1]**2 + p[2]**2 + p[3]**2) for p in pts)
print('max |p| raw', mx, 'raw/65536', mx/65536, 'raw/4', mx/4)
xs=[p[1] for p in pts]; ys=[p[2] for p in pts]; zs=[p[3] for p in pts]
print('bbox raw', min(xs),max(xs),min(ys),max(ys),min(zs),max(zs))
