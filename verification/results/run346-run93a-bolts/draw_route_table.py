#!/usr/bin/env python3
"""Per-draw rows for chosen F8 frames of a session log: index, vs/ps, prims, ZENABLE/ZWRITE/ZFUNC, blend src/dst,
motion_route routed/depth (whether the draw writes the RT2 depth lane), object model/node/flags130, the depth surface
identity, and whether a clear / SetDepthStencilSurface change appears. Prints only depth-writing draws that are NOT
routed (hardware depth the depth lane does not see), the bullet draws, and a per-frame surface-identity census.
usage: draw_route_table.py LOG FRAME [FRAME...]"""
import sys, re, collections
log, frames = sys.argv[1], set(sys.argv[2:])
kv = re.compile(r'(\w+)=(\S+)')
cur = None; rows = collections.defaultdict(list); rec = None; other = collections.defaultdict(collections.Counter)
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('frame_begin'):
            fr = line.split('frame=')[1].split()[0]; cur = fr if fr in frames else None; rec = None; continue
        if cur is None: continue
        k = line.split(' ', 1)[0]
        if k == 'frame_end': cur = None; continue
        if k == 'draw':
            d = dict(kv.findall(line)); rec = {'i': int(d['index']), 'vs': d['vs'][:8], 'ps': d['ps'][:8], 'pr': d['primitives'], 'st': {}, 'ds': None, 'rt': None}
            rows[cur].append(rec); continue
        if k == 'clear': rows[cur].append({'clear': dict(kv.findall(line))}); rec = None; continue
        if k in ('set_depth_stencil', 'set_render_target', 'depth_copy', 'scene_depth') or 'depth_stencil' in k:
            other[cur][k] += 1
        if rec is None: continue
        if k == 'state':
            d = dict(kv.findall(line)); rec['st'][int(d['id'])] = int(d['value'])
        elif k == 'surface':
            d = dict(kv.findall(line))
            if d['role'] == 'depth': rec['ds'] = d.get('ptr') + '/' + d.get('identity')
            if d['role'] == 'rt0': rec['rt'] = d.get('ptr') + '/' + d.get('identity')
        elif k == 'object_context':
            d = dict(kv.findall(line)); rec['model'] = d['model']; rec['node'] = d['node']; rec['f130'] = d['flags130']
        elif k == 'motion_route':
            d = dict(kv.findall(line)); rec['routed'] = d['routed']; rec['rdepth'] = d['depth']
for f in sorted(rows, key=int):
    ds = collections.Counter(r['ds'] for r in rows[f] if 'i' in r); rt = collections.Counter(r['rt'] for r in rows[f] if 'i' in r)
    zw = [r for r in rows[f] if 'i' in r and r['st'].get(14) == 1]
    un = [r for r in zw if r.get('routed') != '1']
    print(f'{f} draws={sum(1 for r in rows[f] if "i" in r)} depth_surfaces={dict(ds)} rt0={dict(rt)} zwrite={len(zw)} zwrite_unrouted={len(un)} other_tags={dict(other[f])}')
    for r in rows[f]:
        if 'clear' in r: print(f'  clear flags={r["clear"].get("flags")} z={r["clear"].get("z")}'); continue
        bullet = r['vs'] == '5e484a06'
        if bullet or (r['st'].get(14) == 1 and r.get('routed') != '1'):
            s = r['st']
            print(f"  d{r['i']:>3} {'BULLET' if bullet else 'ZW-unrouted'} vs={r['vs']} ps={r['ps']} pr={r['pr']} Z={s.get(7)} ZW={s.get(14)} ZF={s.get(23)} {s.get(19)}/{s.get(20)} AT={s.get(15)} CW={s.get(168)} routed={r.get('routed')} lane={r.get('rdepth')} model={r.get('model')} node={r.get('node')} f130={r.get('f130')} ds={r['ds']}")
