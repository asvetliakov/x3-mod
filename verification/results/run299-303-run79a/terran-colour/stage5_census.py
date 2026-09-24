"""Stage-5 (occlusion sampler s5 of the XT PS) binding per draw, grouped by body, LOD and whether the body is in
the installed overlay; census model -> body map from the same frames. Usage: python3 stage5_census.py LOG FRAMES"""
import sys, re, json
from collections import Counter
from pathlib import Path
ADDON = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/addon'
over = {b['name'].lower(): s for s in ('05', '06') for b in json.load(open(ADDON / f'{s}.x3m-lod.json'))['bodies']}
log, frames = sys.argv[1], set(sys.argv[2].split(','))
body, draws, cur = {}, [], None
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('cull_census device'):
            d = dict(re.findall(r'(\w+)=(\S+)', line))
            if d.get('frame') in frames and d.get('body', '-') != '-':
                body[d['model']] = d['body'].replace('\\', '/').lower()
        elif line.startswith('draw '):
            d = dict(re.findall(r'(\w+)=(\S+)', line))
            cur = dict(frame=d['frame'], ps=d.get('ps'), s5='-') if d.get('frame') in frames else None
            if cur: draws.append(cur)
        elif cur is not None:
            if line.startswith('object_context'):
                d = dict(re.findall(r'(\w+)=(\S+)', line)); cur['model'] = d['model']; cur['lod'] = int(d['lod'], 16)
            elif line.startswith('texture stage=5'):
                cur['s5'] = re.search(r'identity=(\d+)', line).group(1)
            elif line.startswith('texture_desc stage=5'):
                d = dict(re.findall(r'(\w+)=(\S+)', line)); cur['s5'] += f":{d['w']}x{d['h']}"
c = Counter()
for d in draws:
    b = body.get(d.get('model'), '?')
    if len(sys.argv) < 4 and d['ps'] != '5f82ecacd39529cd': continue
    c[(b, d.get('lod'), over.get(b, '-'), d['s5'])] += 1
for k, v in sorted(c.items(), key=lambda kv: (kv[0][2], kv[0][0], str(kv[0][1]))):
    print(v, 'lod=%s' % k[1], 'slot=' + k[2], 's5=' + k[3], k[0])
