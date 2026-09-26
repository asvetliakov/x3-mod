#!/usr/bin/env python3
"""Frame-time distribution per state from a session log (one pass, prints only summary rows).
State per frame: menu = last volumetric_fog_sector reason no_cockpit (or none) ; flight = otherwise and a
shadow_replay_depth row on the frame; flight_noreplay = in a flight segment without the row (transitions,
loading); frames with capture!=0 or draws==0 are reported separately. dt is taken from consecutive frame_end
qpc (0.1 us ticks), not the integer dt_ms field. Flight is further split by background index (bgN).
Usage: frame_dt.py <session.log> [--series BIN]"""
import re, sys
path = sys.argv[1]
fe = {}; replay = set(); sect = []; cap = set()
rx = re.compile(r'frame=(\d+) draws=(\d+) capture=(\d+) .* dt_ms=(\d+) qpc=(\d+)')
with open(path, errors='replace') as fh:
    for line in fh:
        if line.startswith('frame_end '):
            m = rx.search(line)
            if m:
                f = int(m.group(1)); fe[f] = (int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)))
        elif line.startswith('shadow_replay_depth '):
            replay.add(int(re.search(r'frame=(\d+)', line).group(1)))
        elif line.startswith('volumetric_fog_sector '):
            m = re.search(r'frame=(\d+).*reason=(\S+).* index=(-?\d+)', line)
            if m: sect.append((int(m.group(1)), m.group(2), int(m.group(3))))
sect.sort()
frames = sorted(fe)
si = 0; cur = ('none', -1)
groups = {}
def add(k, v): groups.setdefault(k, []).append(v)
for i in range(1, len(frames)):
    f = frames[i]; p = frames[i - 1]
    if f != p + 1: continue
    while si < len(sect) and sect[si][0] <= f:
        cur = (sect[si][1], sect[si][2]); si += 1
    draws, capt, dtms, q = fe[f]
    dt = (q - fe[p][3]) / 1e4  # ms
    if capt:
        add('capture', dt); continue
    menu = cur[0].startswith('no_cockpit') or cur[0] == 'none'
    if draws == 0: add('draws0', dt)
    elif menu: add('menu' + ('_ge500' if draws >= 500 else '_lt500'), dt)
    elif f in replay: add('flight', dt); add('flight_bg%d' % cur[1], dt)
    else: add('flight_noreplay', dt)
def pct(v, q): return v[min(len(v) - 1, int(q * (len(v) - 1) + 0.5))]
print('file', path)
print('%-18s %7s %7s %7s %7s %7s %8s %8s' % ('state', 'n', 'p10', 'p50', 'p90', 'p99', 'mean', 'fps@p50'))
for k in sorted(groups):
    v = sorted(groups[k])
    if len(v) < 5: continue
    print('%-18s %7d %7.2f %7.2f %7.2f %7.2f %8.2f %8.1f' % (k, len(v), pct(v, .1), pct(v, .5), pct(v, .9), pct(v, .99), sum(v) / len(v), 1000 / pct(v, .5)))
