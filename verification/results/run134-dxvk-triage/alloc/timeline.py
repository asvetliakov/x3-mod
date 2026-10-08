#!/usr/bin/env python3
"""Run 134 A alloc triage: align DXVK allocation failures in launcher-stderr.log with
session-log frames (qpc -> UTC via clock_anchor) and list per-frame readback results.
Usage: timeline.py <session_dir>"""
import sys, re, glob, datetime as dt, collections
d = sys.argv[1]
L = sorted(glob.glob(d + '/session-*.log'))[0]
anchor = None; frames = []; rb = collections.OrderedDict(); end = None; caps = []
def utc(q):
    return anchor[0] + dt.timedelta(seconds=(q - anchor[1]) / anchor[2])
with open(L, errors='replace') as f:
    for line in f:
        if line.startswith('clock_anchor'):
            kv = dict(t.split('=', 1) for t in line.split()[1:])
            anchor = (dt.datetime.fromisoformat(kv['utc'].replace('Z', '+00:00')), int(kv['qpc']), int(kv['qpc_frequency']))
        elif line.startswith('frame_end '):
            kv = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
            frames.append((int(kv['frame']), int(kv['qpc']), int(kv['capture']), int(kv['elapsed_ms'])))
        elif re.match(r'^(hdr_readback|motion_output_readback|motion_output_depth_readback|shadow_replay_map_readback) ', line):
            kv = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
            rb.setdefault(int(kv['frame']), []).append((kv['file'], kv['result'], int(kv['bytes'])))
        elif line.startswith('session_end'):
            end = line.strip()
fq = {f: q for f, q, c, e in frames}
print('first_frame_end', frames[0][0], utc(frames[0][1]).isoformat())
print('last_frame_end', frames[-1][0], utc(frames[-1][1]).isoformat(), 'elapsed_ms', frames[-1][3])
print(end)
print('captures (frames with readbacks):')
for fr, items in rb.items():
    ok = sum(1 for i in items if i[1] == '00000000'); okb = sum(i[2] for i in items)
    req = sum({'hdr': 58982400, 'mot': 117964800, 'dep': 117964800}.get(i[0][:3], 0) for i in items)
    print(f'  frame={fr} end_utc={utc(fq[fr]).isoformat() if fr in fq else "?"} rows={len(items)} ok={ok} ok_bytes={okb} '
          f'failed={[i[0].split("_1_")[0] for i in items if i[1] != "00000000"]}')
# DXVK failures
S = d + '/launcher-stderr.log'
fails = []
with open(S, errors='replace') as f:
    pend = None
    for line in f:
        m = re.match(r'^\[([^\]]+)\] err:\s+Size:\s+(\d+)', line)
        if m: pend = (m.group(1), int(m.group(2)))
        m2 = re.match(r'^\[([^\]]+)\] err:\s+Heap 0: (\d+) MB allocated, (\d+) MB used', line)
        if m2 and pend: fails.append(pend + (int(m2.group(2)), int(m2.group(3)))); pend = None
        if 'terminate called' in line: print('terminate', line.strip()[:120])
print('dxvk failure blocks', len(fails))
cl = collections.OrderedDict()
for ts, sz, a, u in fails:
    k = ts[:19]
    cl.setdefault(k, collections.Counter())[sz] += 1
for k, c in cl.items():
    t = dt.datetime.fromisoformat(k + '+00:00')
    near = min(frames, key=lambda fr: abs((utc(fr[1]) - t).total_seconds()))
    print(f'  {k}Z sizes={dict(c)} nearest_frame={near[0]} capture={near[2]}')
print('heap_allocated_MB', sorted(set(f[2] for f in fails)), 'heap_used_MB', sorted(set(f[3] for f in fails)))
