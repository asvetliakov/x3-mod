import sys
from pathlib import Path
WT=Path(sys.argv[1]); sys.path.insert(0,str(WT/'tools/analysis'))
import bob1, lod_overlay, body_materials
GAME=Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets,_=lod_overlay.original_assets(GAME)
for name,mi in (('ships/split/split_TL',14),('ships/teladi/teladi_M6',27),('stations/trading_stations/teladi_trading_station_partA',26)):
    tree=bob1.parse(assets.read_entry(bob1.resolve_body(assets,name)),8); mats=bob1.materials(tree); r0=bob1.lods(tree)[0]
    m=mats[mi]; alpha=lod_overlay.alpha_materials(mats)
    ps={n.decode():(v.decode('latin1') if t==8 else list(v)[:4]) for n,t,v in m['params']}
    keys=[k for k in ps if k.startswith('t_') and isinstance(ps[k],str) or k.lower() in ('g_alphablendenable','g_alphatestenable','g_srcblend','g_destblend','g_matemissivecolor','g_matdiffusestrength','g_alphavalue')]
    nf=sum(len(g['faces']) for p in r0['parts'] for g in p['groups'] if g['material']==mi)
    print(name, mi, m.get('effect'), 'tech', m.get('technique'), 'alpha' if mi in alpha else 'opaque', 'faces r0', nf, {k:ps[k] for k in keys}, 'name', m.get('name'))
