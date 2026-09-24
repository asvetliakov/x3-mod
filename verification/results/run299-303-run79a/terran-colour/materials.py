"""List per-LOD groups and material params/textures of Terran bodies (installed overlay, read-only).
Usage: python3 materials.py body [body...]"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'analysis'))
import bob1

def fmt_param(p):
    name, typ, v = p
    if isinstance(v, (list, tuple)):
        v = ','.join(str(x) for x in v)
    return f'{name}={v}'

for body in sys.argv[1:]:
    data, prov = bob1.load(body)
    tree = bob1.parse(data) if bob1.kind(data) == 'BOB1' else bob1.parse_text(data)
    print('==', body, prov)
    mats = bob1.materials(tree)
    for li, lod in enumerate(bob1.lods(tree)):
        rows = []
        for p in lod['parts']:
            for g in p['groups']:
                rows.append(f"m{g['material']}:{len(g['faces'])}")
        print(f'  LOD{li} value={lod["value"]} groups', ' '.join(rows))
    for m in mats:
        if 'effect' in m:
            print(f"  mat{m['index']} flags={m['flags']:#x} tech={m['technique']} effect={m['effect']}")
            for p in m['params']:
                print('     ', fmt_param(p))
        else:
            print(f"  mat{m['index']} flags={m.get('flags')} tex={m['texture']} colors={m['colors']} maps={m['maps']}")
