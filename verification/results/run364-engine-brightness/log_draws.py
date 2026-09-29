#!/usr/bin/env python3
"""Run 364 burst: per-draw table of one capture frame (non-draw capture events, readbacks, and every draw with vs/ps,
primitives, rt0 identity/format, ZENABLE/ZWRITE, SRCBLEND/DESTBLEND/sepalpha, stage-0 texture), from the session log.
Usage: log_draws.py <log> <frame> [vs_prefix]"""
import re, sys
L, F = sys.argv[1], sys.argv[2]; want = sys.argv[3] if len(sys.argv) > 3 else None
tag = f' frame={F} '
draws = []; cur = None; events = []; inframe = False
kv = lambda s: dict(re.findall(r'(\w+)=(\S+)', s))
for line in open(L, errors='replace'):
    if line.startswith('frame_begin') :
        inframe = tag in line.strip() + ' '
        continue
    if not inframe: 
        if line.startswith(('hdr_frame', 'hdr_readback')) and tag in line: events.append(line[:200].strip())
        continue
    k = line.split(' ', 1)[0]
    if k == 'capture_event':
        d = kv(line)
        if d.get('op') != 'draw_begin': events.append(f"event after_draw={d.get('after_draw')} op={d.get('op')}")
    elif k == 'draw' and tag in line:
        cur = kv(line); cur['st'] = {}; draws.append(cur)
    elif k == 'state' and cur is not None:
        d = kv(line); cur['st'][d['id']] = d['value']
    elif k == 'surface' and cur is not None and 'role=rt0' in line:
        d = kv(line); cur['rt0'] = f"{d['identity']}/{d['format']}/{d['width']}"
    elif k == 'texture_desc' and cur is not None and 'stage=0 ' in line:
        d = kv(line); cur['tex0'] = f"{d['w']}x{d['h']}/{d['format']}"
    elif k == 'texture' and cur is not None and 'stage=0 ' in line:
        cur['tex0id'] = kv(line).get('identity')
    elif k in ('hdr_readback', 'hdr_frame') or ('readback' in k and tag in line):
        events.append(f'{k} after_draws={len(draws)} ' + line[:160].strip())
for e in events: print(e)
print('index vs ps prims rt0 zen zwr src dst sepa tex0id tex0')
for d in draws:
    if want and not d['vs'].startswith(want): continue
    s = d['st']
    print(d['index'], d['vs'][:8], d['ps'][:8], d['primitives'], d.get('rt0'), s.get('7'), s.get('14'), s.get('19'), s.get('20'), s.get('206'), d.get('tex0id'), d.get('tex0'))
