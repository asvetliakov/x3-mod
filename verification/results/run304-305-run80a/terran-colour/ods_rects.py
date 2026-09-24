"""Screen rectangles (object_bounds sx0..sy1, 5120x1440) of the ODS (usc_dock_e_*) draws in given frames, with the
draw index, lod and prims; plus the union rectangle per frame. Streaming, read-only. Usage: ods_rects.py LOG FRAMES"""
import sys, re
KV = re.compile(r'(\w+)=(\S*)')
log, frames = sys.argv[1], set(sys.argv[2].split(','))
body, cur, out = {}, None, []
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('cull_census device'):
            d = dict(KV.findall(line))
            if d.get('body', '-') != '-': body[d['node']] = d['body'].rsplit('\\', 1)[-1]
        elif line.startswith('draw device'):
            d = dict(KV.findall(line)); cur = d if d['frame'] in frames else None
        elif cur is not None and line.startswith('object_context'):
            d = dict(KV.findall(line)); cur['node'] = d['node']; cur['lod'] = int(d['lod'], 16)
        elif cur is not None and line.startswith('object_bounds device'):
            d = dict(KV.findall(line))
            if 'usc_dock_e' in body.get(cur.get('node'), ''):
                out.append((cur['frame'], cur['index'], body[cur['node']], cur['lod'], cur['primitives'],
                            *(round(float(d[k])) for k in ('sx0', 'sy0', 'sx1', 'sy1'))))
for r in out: print(*r)
for f in sorted(frames):
    rs = [r for r in out if r[0] == f]
    if rs: print('union', f, min(r[5] for r in rs), min(r[6] for r in rs), max(r[7] for r in rs), max(r[8] for r in rs))
