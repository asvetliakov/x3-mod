import sys, json, collections
from pathlib import Path
WT=Path(sys.argv[1]); sys.path.insert(0,str(WT/'tools/analysis'))
import bob1, lod_overlay, lod_atlas
GAME=Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
rec=json.loads((GAME/'addon/x3m-lod-batch.json').read_text())
assets,_=lod_overlay.original_assets(GAME)
NEW={'glass.fx','asteroid.fx','planet_haze.fx','adeffects.fx','effects.fx'}
seen=collections.Counter()
for b in rec['bodies']:
    if b.get('filter') or 'dominant_slot_missing' not in (b.get('refuse') or ()): continue
    try: entry=bob1.resolve_body(assets,b['name'])
    except bob1.FormatError: entry=next(iter(assets.candidates('objects/'+b['name'].lower()+'.bob')))
    tree=bob1.parse(assets.read_entry(entry),8); mats=bob1.materials(tree); r0=bob1.lods(tree)[0]
    alpha=lod_overlay.alpha_materials(mats)
    opaque=[g for p in r0['parts'] if not p['flags']&lod_atlas.HIDDEN_PART for g in p['groups'] if g['material'] not in alpha]
    cl=lod_atlas.effect_classes(mats,opaque)
    print(b['name'], 'alpha',sorted(alpha),'classes',[(e.decode() if isinstance(e,bytes) else e,m) for e,m in cl])
    for e,mis in cl:
        en=(e.decode() if isinstance(e,bytes) else str(e)).lower()
        if any(n in en for n in NEW):
            for m in mis:
                ps=[(n.decode(),t,v if t==8 else (v[:4] if hasattr(v,'__len__') else v)) for n,t,v in mats[m]['params']]
                key=str(ps)
                if seen[key]==0: print('   mat',m,mats[m].get('technique'),ps)
                seen[key]+=1
