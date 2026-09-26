#!/usr/bin/env python3
"""Frames with draws>=500 and 290..310: are they in flight (a shadow_replay_depth row on the same frame,
and the last volumetric_fog_sector row before them is not reason=no_cockpit) or in the menu?
Usage: plateau_check.py <session.log>..."""
import re, sys
for path in sys.argv[1:]:
    fe = {}; replay = set(); sect = []
    with open(path, errors='replace') as fh:
        for line in fh:
            if line.startswith('frame_end '):
                m = re.search(r'frame=(\d+) draws=(\d+)', line); fe[int(m.group(1))] = int(m.group(2))
            elif line.startswith('shadow_replay_depth '):
                replay.add(int(re.search(r'frame=(\d+)', line).group(1)))
            elif line.startswith('volumetric_fog_sector '):
                m = re.search(r'frame=(\d+).*reason=(\S+).* index=(-?\d+)', line); sect.append((int(m.group(1)), m.group(2) + '/bg' + m.group(3)))
    def state(f):
        r = 'none'
        for fr, reason in sect:
            if fr <= f: r = reason
        return 'menu' if r.startswith('no_cockpit') or r == 'none' else 'flight'
    out = []
    for name in ('menu', 'flight'):
        v = sorted(d for f, d in fe.items() if d > 0 and state(f) == name)
        if v: out.append(f'{name}: n={len(v)} p50={v[len(v)//2]} p90={v[int(.9*(len(v)-1))]} max={v[-1]}')
    for lo, hi in ((500, 10**6), (290, 310)):
        fr = [f for f, d in fe.items() if lo <= d <= hi]
        menu = sum(state(f) == 'menu' for f in fr); rep = sum(f in replay for f in fr)
        flight = sorted(f for f in fr if state(f) == 'flight')
        out.append(f'draws{lo}-{hi if hi < 10**6 else "max"}: frames={len(fr)} menu={menu} flight={len(flight)} with_shadow_replay={rep}'
                   f' flight_frames={flight[:1] + flight[-1:] if flight else []}')
    print(path.split('/')[2], '; '.join(out))
    bounds = [fr for fr, _ in sect] + [max(fe) + 1]
    for i, (fr, reason) in enumerate(sect):
        v = sorted(d for f, d in fe.items() if fr <= f < bounds[i + 1] and d > 0)
        if v and not reason.startswith('no_cockpit'):
            print(f'  segment frames {fr}-{bounds[i+1]-1} sector_reason={reason} n={len(v)} p50={v[len(v)//2]} p90={v[int(.9*(len(v)-1))]} max={v[-1]} ge290={sum(x >= 290 for x in v)}')
