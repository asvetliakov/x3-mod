import sys
from pathlib import Path
WT=Path(sys.argv[1]); sys.path.insert(0,str(WT/'tools/analysis'))
import bob1, lod_overlay, lod_atlas
GAME=Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets,_=lod_overlay.original_assets(GAME)
for name,mi in (('stations/x3ap/StockmarketBoardS',7),('stations/x3ap/StockmarketBoardXL',7),('stations/station_scenes/lost_colony/lostcolony_energy',33),('ships/argon/argon_M3',1)):
    tree=bob1.parse(assets.read_entry(bob1.resolve_body(assets,name)),8); r0=bob1.lods(tree)[0]
    us=[];vs=[]
    for p in r0['parts']:
        for g in p['groups']:
            if g['material']==mi:
                for f in g['faces']:
                    for i in f[:3] if isinstance(f,(list,tuple)) else f['points']:
                        u,v=lod_atlas.point_uv(r0['points'][i]); us.append(u); vs.append(v)
    print(name, mi, 'n', len(us), 'u', (round(min(us),3), round(max(us),3)) if us else None, 'v', (round(min(vs),3), round(max(vs),3)) if vs else None, 'parts', len(r0['parts']), 'groups', sum(len(p['groups']) for p in r0['parts']))
