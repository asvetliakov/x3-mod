#!/usr/bin/env python3
"""Run 264 (Run 70 B) dust-mote log summary: fog frame fields per toggle span, frame_timing windows."""
import re, sys, statistics as st
from collections import Counter
L = sys.argv[1] if len(sys.argv) > 1 else '/tmp/x3-bottleX3-run264/session-20260923-051549-216.log'
kv = lambda s: dict(re.findall(r'(\w+)=(\S+)', s))
toggles, fog, ft, other = [], [], [], Counter()
for line in open(L, errors='replace'):
    t = line.split(' ', 1)[0]
    if t == 'fog_dust_motes_toggle': toggles.append(kv(line))
    elif t == 'volumetric_fog_frame': fog.append(kv(line))
    elif t == 'frame_timing': ft.append(kv(line))
    elif re.search(r'refus|lost|reset|cut_|camera_cut|view_switch', t): other[t] += 1
print('toggles', [(d['frame'], d['enabled']) for d in toggles])
ev = [(int(d['frame']), d['enabled'] == '1') for d in toggles]
def state(f):
    s = True
    for fr, e in ev:
        if f >= fr: s = e
    return s
print('fog rows', len(fog), 'applied', Counter(d['applied'] for d in fog), 'reasons', Counter(d.get('reason') for d in fog if d['applied']=='0').most_common(6))
for on in (True, False):
    a = [d for d in fog if d['applied'] == '1' and state(int(d['frame'])) == on]
    if not a: continue
    q = lambda k: [float(d[k]) for d in a]
    pc = lambda v, p: sorted(v)[int(p*(len(v)-1))]
    sh = q('mote_shift_px')
    print(f"motes_on={on} applied={len(a)} motes={Counter(d['motes'] for d in a)} count={Counter(d['mote_count'] for d in a)} calls={Counter(d['mote_calls'] for d in a)} "
          f"streak={Counter(d['mote_streak'] for d in a)} shadow={Counter(d['mote_shadow'] for d in a)} refused={Counter(d['mote_refused'] for d in a)} devcalls={Counter(d['calls'] for d in a).most_common(3)}")
    print(f"  shift_px p10/50/90/max {pc(sh,.1):.1f}/{pc(sh,.5):.1f}/{pc(sh,.9):.1f}/{max(sh):.1f}  cpu_us p50/p95 {pc(q('cpu_us'),.5):.1f}/{pc(q('cpu_us'),.95):.1f}")
for d in ft:
    f = int(d['frame']); lo = f - int(d['frames']) + 1
    s = {state(x) for x in range(lo, f + 1)}
    print('ft', lo, f, 'on' if s == {True} else 'off' if s == {False} else 'mixed', 'dt_p50', d['dt_p50_us'], 'p95', d['dt_p95_us'], 'draws', d['draws_p50'], 'slow', d['slow'])
for fr in (5048, 5055, 6411, 6418):
    d = next((x for x in fog if x['frame'] == str(fr)), None)
    if d: print('cap', fr, {k: d[k] for k in ('applied','motes','mote_shift_px','mote_streak','cpu_us','calls','strength','density_scale')})
print('other', dict(other))
