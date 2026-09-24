"""Installed slot-06 marker (bottle X3, read-only): per ODS body (usc_dock_e_*) the merged record's atlas tiles
(material ids, diffuse/light source names, tile size, share), the synthesised material (absorbed ids, the params
the baker wrote vs the dominant source value) and the atlas occlusion entry. Usage: python3 ods_tiles.py [SUBSTR]"""
import json, sys
from pathlib import Path
ADDON = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/addon'
sub = sys.argv[1] if len(sys.argv) > 1 else 'usc_dock_e'
short = lambda s: s.replace('/', '\\').rsplit('\\', 1)[-1] if s else s
for b in json.load(open(ADDON / '06.x3m-lod.json'))['bodies']:
    if sub not in b['name']: continue
    a = b['atlas']
    print(f"== {b['name'].rsplit('/', 1)[-1]} source_record={b['source_record']} new_lod={b['new_lod']} pad_lod={b['pad_lod']}"
          f" thresholds={b['thresholds']} source_materials={b['source_materials']} draws={b['draws']} groups={b['groups']}"
          f" atlas={a['size']} material={a.get('material')} dominant={a.get('dominant')} glow={b.get('glow')}")
    print('  occlusion:', json.dumps(a.get('occlusion'))[:300])
    print('  atlas.materials:', json.dumps(a.get('materials'))[:300], ' split_groups:', a.get('split_groups'))
    for t in a['tiles']:
        n = t['names']
        print(f"  tile mats={t['mats']} diff={short(n.get('diffuse'))} light={short(n.get('light'))} spec={short(n.get('specular'))}"
              f" content={t['content']} origin={t['origin']} share={t.get('share')} tpp={t.get('texels_per_px')}")
    for s in b.get('synth', []):
        print(f"  synth index={s['index']} dominant={s['dominant']} absorbed={s['absorbed']}")
        for p in s['params']:
            if p.get('dominant') != p.get('written'):
                print(f"    {p['name']}: dominant={p.get('dominant')} mean={p.get('mean')} written={p.get('written')}"
                      f"  ({p.get('dominant', 0) / 65536:.3f} -> {p.get('written', 0) / 65536:.3f})" if isinstance(p.get('written'), (int, float)) else f"    {p}")
