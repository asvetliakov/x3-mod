#!/usr/bin/env python3
"""Run175 A, Q2: per ship_steering_summary interval, handler_us_max against the events inside it (gate launches, gate arrivals
(ship_gate_transition), mesh_body loads (loading_probe), the frame's worst dt/gap_pre from frame_timing_slow).
Usage: python3 -I spike_corr.py LOG"""
import sys, re, bisect
log = sys.argv[1]
summ = []; launches = []; trans = []; mesh = []; slow = []; cur = 0
fre = re.compile(rb' frame=(\d+)')
with open(log, 'rb') as f:
    for line in f:
        k = line[:40].split(b' ', 1)[0]
        if k == b'frame_end':
            cur = int(fre.search(line).group(1)) + 1
        elif k == b'ship_steering_summary':
            d = dict(re.findall(rb'(\w+)=(\S+)', line)); summ.append((int(d[b'frame']), float(d[b'handler_us_max']), int(d[b'calls'])))
        elif k == b'ship_gate_launch':
            launches.append(int(fre.search(line).group(1)))
        elif k == b'ship_gate_transition':
            trans.append(int(fre.search(line).group(1)))
        elif k == b'loading_probe' and b'site=mesh_body' in line:
            d = dict(re.findall(rb'(\w+)=(\S+)', line)); mesh.append((cur, int(d[b'calls']), float(d[b'max_us'])))
        elif k == b'frame_timing_slow':
            d = dict(re.findall(rb'(\w+)=(\S+)', line)); slow.append((int(d[b'frame']), int(d[b'dt_us']), int(d[b'gap_pre_us'])))
fr = [s[0] for s in summ]
def iv(frame):
    i = bisect.bisect_left(fr, frame); return i if i < len(fr) else None
L = {}; T = {}; M = {}; SL = {}
for x in launches: i = iv(x); L[i] = L.get(i, 0) + 1
for x in trans: i = iv(x); T[i] = T.get(i, 0) + 1
for c, n, mx in mesh: i = iv(c - 1); M[i] = max(M.get(i, 0), mx)   # mesh rows are emitted at the next flush: attributed to the interval of emission (approximate)
for fno, dt, gp in slow:
    i = iv(fno)
    if i is not None and gp > SL.get(i, (0, 0, 0))[2]: SL[i] = (fno, dt, gp)
print("intervals", len(summ), "launch rows", len(launches), "transition rows", len(trans), "mesh_body rows", len(mesh))
print("top intervals by handler_us_max: frame_end_of_interval max_us calls launches transitions mesh_body_max_us(emitted) worst_slow(frame,dt_us,gap_pre_us)")
for i in sorted(range(len(summ)), key=lambda i: -summ[i][1])[:10]:
    print(f" {summ[i][0]} {summ[i][1]} {summ[i][2]} L={L.get(i,0)} T={T.get(i,0)} mesh={M.get(i,0)} slow={SL.get(i)}")
li = set(L); hi = [summ[i][1] for i in li]; lo = [summ[i][1] for i in range(len(summ)) if i not in li]
q = lambda a, p: sorted(a)[int(p * (len(a) - 1))] if a else None
print(f"intervals with a launch: n={len(hi)} median max_us={q(hi,.5)} p90={q(hi,.9)} >1000us={sum(x>1000 for x in hi)}")
print(f"intervals without:       n={len(lo)} median max_us={q(lo,.5)} p90={q(lo,.9)} >1000us={sum(x>1000 for x in lo)}")
print("worst gap_pre frames overall (frame,dt_us,gap_pre_us):", sorted(slow, key=lambda s: -s[2])[:6])
