#!/usr/bin/env python3
"""Per-draw table for the F8 capture frames of run339 (Run 91 A launch 2).
Streams the session log; prints one line per draw/clear in the requested frames:
index, vs, ps, primitives, ZENABLE(7) ZWRITE(14) ZFUNC(23) ABLEND(27) SRC(19) DST(20),
viewport minz/maxz, projection rows 2/3 (floats), object model/flags130, rt0/depth identity.
usage: draw_table.py LOG FRAME [FRAME...]"""
import sys, struct, re
log, frames = sys.argv[1], set(sys.argv[2:])
def f(h): return struct.unpack('<f', bytes.fromhex(h)[::-1])[0]
cur = None; rec = None; out = []
def flush():
    global rec
    if rec: out.append(rec)
    rec = None
kv = re.compile(r'(\w+)=(\S+)')
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('frame_begin'):
            fr = line.split('frame=')[1].split()[0]
            cur = fr if fr in frames else None
            continue
        if cur is None: continue
        k = line.split(' ', 1)[0]
        if k == 'frame_end':
            flush(); d = dict(kv.findall(line)); out.append({'kind': 'end', 'frame': cur, 'draws': d.get('draws')}); cur = None; continue
        if k == 'draw':
            flush(); d = dict(kv.findall(line)); rec = {'kind': 'draw', 'frame': cur, 'i': d['index'], 'vs': d.get('vs'), 'ps': d.get('ps'), 'prims': d.get('primitives'), 'st': {}}
        elif k == 'clear':
            flush(); d = dict(kv.findall(line)); out.append({'kind': 'clear', 'frame': cur, 'flags': d['flags'], 'z': d['z']})
        elif rec is None: continue
        elif k == 'state':
            d = dict(kv.findall(line)); rec['st'][int(d['id'])] = int(d['value'])
        elif k == 'viewport':
            d = dict(kv.findall(line)); rec['vp'] = (d['minz'], d['maxz'])
        elif k == 'object_matrix' and 'role=projection' in line:
            d = dict(kv.findall(line))
            if d['row'] in ('2', '3'): rec['p' + d['row']] = tuple(round(f(b), 6) for b in d['bits'].split(',')[2:4])
        elif k == 'object_context':
            d = dict(kv.findall(line)); rec['model'] = d.get('model'); rec['f130'] = d.get('flags130'); rec['node'] = d.get('node')
        elif k == 'surface':
            d = dict(kv.findall(line)); rec[d['role']] = d.get('identity','?') + ':' + d.get('width','?') + 'x' + d.get('height','?') + ':f' + d.get('format','?')
for r in out:
    if r['kind'] != 'draw': print(r); continue
    s = r['st']
    print(f"{r['frame']} d{r['i']:>3} vs={r['vs'][:8]} ps={r['ps'][:8]} pr={r['prims']:>5} Z={s.get(7)} ZW={s.get(14)} ZF={s.get(23)} AB={s.get(27)} {s.get(19)}/{s.get(20)} AT={s.get(15)} vp={r.get('vp')} p2={r.get('p2')} p3={r.get('p3')} model={r.get('model')} f130={r.get('f130')} rt={r.get('rt0')} ds={r.get('depth')}")
