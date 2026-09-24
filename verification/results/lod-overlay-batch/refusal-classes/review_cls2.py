import sys, json, collections
from pathlib import Path
WT=Path(sys.argv[1]); sys.path.insert(0,str(WT/'tools/analysis'))
import bob1, lod_overlay, lod_atlas
GAME=Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
rec=json.loads((GAME/'addon/x3m-lod-batch.json').read_text())
assets,_=lod_overlay.original_assets(GAME)
NEW=('glass','asteroid','planet_haze','adeffects','effects.fx')
STD={'nofiltering','autofree','tex2','texanimstartu','texanimstartv','texanimendu','texanimendv','texanimduration','texanimrotation','texanimoriginu','texanimoriginv','brightness','contrast','saturation','hue','colormatrix','diffcompression','bumpcompression','speccompression','lightcompression'}
seen=set()
for b in rec['bodies']:
    if b.get('filter') or 'dominant_slot_missing' not in (b.get('refuse') or ()): continue
    try: entry=bob1.resolve_body(assets,b['name'])
    except bob1.FormatError: entry=next(iter(assets.candidates('objects/'+b['name'].lower()+'.bob')))
    tree=bob1.parse(assets.read_entry(entry),8); mats=bob1.materials(tree); r0=bob1.lods(tree)[0]
    alpha=lod_overlay.alpha_materials(mats)
    opaque=[g for p in r0['parts'] if not p['flags']&lod_atlas.HIDDEN_PART for g in p['groups'] if g['material'] not in alpha]
    for e,mis in lod_atlas.effect_classes(mats,opaque):
        en=(e.decode() if isinstance(e,bytes) else str(e)).lower()
        if not any(n in en for n in NEW): continue
        for m in mis:
            ps=mats[m]['params']
            anim=[(n.decode(),v) for n,t,v in ps if n.lower().startswith(b'texanim') and n.lower() not in (b'texanimoriginu',b'texanimoriginv') and any(v)]
            tex=[(n.decode(),v.decode('latin1')) for n,t,v in ps if t==8]
            other=[(n.decode(),list(v) if hasattr(v,'__iter__') else v) for n,t,v in ps if t!=8 and n.decode().lower() not in STD and n.lower().startswith(b'g_') is False]
            g=[(n.decode(),list(v)[:4]) for n,t,v in ps if n.lower().startswith(b'g_') and t!=8]
            k=(en,str(tex),str(anim),str(g))
            if k in seen: continue
            seen.add(k)
            print(b['name'],en,'m',m,'tech',mats[m].get('technique'),'\n  tex',tex,'\n  anim',anim,'\n  g',g,'\n  other',other)
