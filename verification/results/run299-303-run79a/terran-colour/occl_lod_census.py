"""s5 (t_OcclusionTexture of the XT pixel shader 5f82ecacd39529cd) binding against the drawn node's LOD index, over a
whole session log (streaming, read-only). Tests the static rule of texture-lookup.md section 12: the engine binds the
NONE_OCCL_DECAL placeholder (*0x00606f74) whenever node+0x14c != 0. Per (lod != 0, overlay slot) it prints the draw
count and the s5 textures (session identity, WxH, levels) with counts.  Usage: python3 occl_lod_census.py LOG [LOG ...]"""
import sys, re, json
from collections import Counter, defaultdict
from pathlib import Path
ADDON = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/addon'
over = {b['name'].lower(): s for s in ('05', '06') for b in json.load(open(ADDON / f'{s}.x3m-lod.json'))['bodies']}
PS = '5f82ecacd39529cd'
for log in sys.argv[1:]:
    body, draws, cur = {}, [], None
    with open(log, errors='replace') as fh:
        for line in fh:
            if line.startswith('cull_census device'):
                d = dict(re.findall(r'(\w+)=(\S+)', line))
                if d.get('body', '-') != '-':
                    body[d['model']] = d['body'].replace('\\', '/').lower()
            elif line.startswith('draw '):
                d = dict(re.findall(r'(\w+)=(\S+)', line))
                cur = dict(s5='-', lvl='-', wh='-') if d.get('ps') == PS else None
                if cur: draws.append(cur)
            elif cur is not None:
                if line.startswith('object_context'):
                    d = dict(re.findall(r'(\w+)=(\S+)', line)); cur['model'] = d['model']; cur['lod'] = int(d['lod'], 16)
                elif line.startswith('texture stage=5'):
                    d = dict(re.findall(r'(\w+)=(\S+)', line)); cur['s5'] = d['identity']; cur['lvl'] = d.get('levels')
                elif line.startswith('texture_desc stage=5'):
                    d = dict(re.findall(r'(\w+)=(\S+)', line)); cur['wh'] = f"{d['w']}x{d['h']}"
    groups = defaultdict(Counter)
    for d in draws:
        if 'lod' not in d: continue
        b = body.get(d['model'], '?')
        groups[(d['lod'] != 0, over.get(b, '-'))][f"{d['s5']}:{d['wh']}:L{d['lvl']}"] += 1
    print('==', Path(log).parent.name, 'XT PS draws with a node:', sum(sum(c.values()) for c in groups.values()))
    for (nz, slot), c in sorted(groups.items()):
        print(f"  lod{'>0' if nz else '=0'} slot={slot} draws={sum(c.values())} distinct_s5={len(c)} top:",
              ' '.join(f'{k}x{v}' for k, v in c.most_common(4)))
