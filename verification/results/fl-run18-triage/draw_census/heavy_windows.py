#!/usr/bin/env python3
"""Per-frame draw composition from aggregate rows (no per-draw rows exist in these logs).
usage: heavy_windows.py LOG FIRST_WINDOW LAST_WINDOW [window_frames=300]
Fields per frame: frame_end draws; motion_output_frame routed (depth-writing scene draws the motion pass routes);
hull_lightmap_frame admitted (draws with a hull light-map pair); engine_frame candidates / not_jet (draws of the
engine effect pair, and those whose body is not on the JET list); shadow_replay_candidates leased (shadow casters);
hull_emission_frame alpha (alpha-blended hull draws refused); sector from sector_background.
Prints medians and p90 per window and over the range; numbers only."""
import re, sys, statistics as st, collections
log, w0, w1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
W = int(sys.argv[4]) if len(sys.argv) > 4 else 300
lo, hi = (w0 - 1) * W, w1 * W
want = {'frame_end': ['draws'], 'motion_output_frame': ['routed'], 'hull_lightmap_frame': ['admitted'],
        'engine_frame': ['candidates', 'not_jet', 'bodies'], 'shadow_replay_candidates': ['leased'],
        'hull_emission_frame': ['alpha', 'opaque'], 'volumetric_fog_cards': ['observed']}
fr = collections.defaultdict(dict)
sector = {}
cur = '?'
fre = re.compile(r' frame=(\d+)')
for line in open(log, errors='replace'):
    k = line.split(' ', 1)[0]
    if k == 'sector_background':
        m = re.search(r' name="([^"]*)"', line); cur = m.group(1) if m and m.group(1) else cur
        continue
    if k not in want or ' device=1 ' not in line:
        continue
    m = fre.search(line)
    n = int(m.group(1))
    if not lo <= n < hi:
        continue
    for f in want[k]:
        m = re.search(r' %s=(\d+)' % f, line)
        if m: fr[n][k + '.' + f] = int(m.group(1))
    sector[n] = cur
cols = [k + '.' + f for k, fs in want.items() for f in fs]
def row(name, frames):
    out = [name, f'n={len(frames)}']
    for c in cols:
        v = [fr[n].get(c, 0) for n in frames]
        out.append(f'{c.split(".", 1)[1]}={st.median(v):.0f}/{sorted(v)[int(0.9 * (len(v) - 1))]}' if v else f'{c}=-')
    return ' '.join(out)
print('median/p90 per window; ' + ' '.join(cols))
for w in range(w0, w1 + 1):
    fs = [n for n in fr if (w - 1) * W <= n < w * W and 'frame_end.draws' in fr[n]]
    if fs: print(row(f'w{w} {collections.Counter(sector[n] for n in fs).most_common(1)[0][0]}', fs))
allf = [n for n in fr if 'frame_end.draws' in fr[n]]
print(row('range', allf))
heavy = [n for n in allf if fr[n]['frame_end.draws'] >= 1500]
if heavy:
    print(row('draws>=1500', heavy))
    sh = [(fr[n].get('motion_output_frame.routed', 0) + fr[n].get('engine_frame.candidates', 0)) / fr[n]['frame_end.draws'] for n in heavy]
    print(f'draws>=1500: (routed + engine candidates) / draws median={st.median(sh):.3f}')
    top = max(heavy, key=lambda n: fr[n]['frame_end.draws'])
    print('max frame', top, fr[top])
