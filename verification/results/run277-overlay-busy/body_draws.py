#!/usr/bin/env python3
"""Run 74 A (run277): per-draw snapshot of one model in selected frames: primitives, ps, lod,
object_bounds, key render states (7 zenable, 14 zwrite, 15 alphatest, 24 alpharef, 25 alphafunc,
27 alphablend, 19/20 blend), textures per stage (identity, w x h, format).
Usage: body_draws.py LOG MODEL FRAME [FRAME...]   (streams the log once)."""
import re, sys
log, model, frames = sys.argv[1], sys.argv[2], set(sys.argv[3:])
KEEP = {'7', '14', '15', '19', '20', '24', '25', '27'}
cur = None; out = []
def flush():
    if cur and cur.get('model') == model: out.append(cur)
for line in open(log, errors='replace'):
    if line.startswith('draw device='):
        flush(); f = re.search(r' frame=(\d+) ', line).group(1)
        cur = None
        if f in frames:
            cur = dict(frame=f, index=int(re.search(r' index=(\d+)', line).group(1)),
                       prim=int(re.search(r'primitives=(\d+)', line).group(1)),
                       ps=re.search(r' ps=(\w+)', line).group(1)[:8], st={}, tex={})
        continue
    if cur is None: continue
    k = line.split(' ', 1)[0]
    if k == 'object_context':
        cur['model'] = re.search(r' model=(\w+)', line).group(1); cur['lod'] = int(re.search(r' lod=(\w+)', line).group(1), 16)
    elif k == 'state':
        m = re.match(r'state id=(\d+) value=(\S+)', line)
        if m and m.group(1) in KEEP: cur['st'][m.group(1)] = m.group(2)
    elif k == 'texture':
        m = re.match(r'texture stage=(\d+) ptr=\w+ type=\d+ identity=(\d+)', line)
        if m: cur['tex'][int(m.group(1))] = [m.group(2)]
    elif k == 'texture_desc':
        m = re.match(r'texture_desc stage=(\d+) w=(\d+) h=(\d+)', line)
        if m and int(m.group(1)) in cur['tex']: cur['tex'][int(m.group(1))].append(f'{m.group(2)}x{m.group(3)}')
    elif k == 'object_bounds':
        m = re.search(r'sx0=(\S+) sy0=(\S+) sx1=(\S+) sy1=(\S+)', line); cur['bb'] = tuple(float(x) for x in m.groups())
flush()
for d in out:
    st = ' '.join(f'{k}={d["st"].get(k, "-")}' for k in ('7', '14', '15', '24', '25', '27', '19', '20'))
    tex = ' '.join(f's{s}:{"/".join(v)}' for s, v in sorted(d['tex'].items()) if s < 4)
    bb = ','.join(f'{x:.0f}' for x in d.get('bb', ()))
    print(f'f={d["frame"]} i={d["index"]} lod={d["lod"]} prim={d["prim"]} ps={d["ps"]} {st} bb=[{bb}] {tex}')
