#!/usr/bin/env python3
"""Per 300-frame window: residual prepare vs light selection 0x0047d5e0 and
view_setup (cull/LOD 0x0047cfe0 runs there), from one x3m.log.
Usage: light_vs_prepare.py [log]   (default /mnt/c/X3/x3m.log)"""
import sys, re
log = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/x3m.log'
want = {'residual_phases': ['prepare_p50_us', 'passes_p50', 'views_p50'],
        'frame_phases': ['dt_p50_us', 'view_setup_p50_us', 'view_submit_p50_us'],
        'light_phases': ['traversal_calls_p50', 'traversal_p50_us', 'traversal_p95_us', 'cockpit_calls_p50', 'cockpit_p50_us', 'valid_frames'],
        'submit_phases': ['material_p50_us', 'world_calls_p50', 'walk_calls_p50'],
        'collide_census': ['p1_pairs_p50']}
rows = {}
for line in open(log, errors='replace'):
    t = line.split(' ', 1)[0]
    if t not in want: continue
    kv = dict(re.findall(r'(\w+)=(\S+)', line))
    f = int(kv.get('frame', 0)) // 300 * 300
    r = rows.setdefault(f, {})
    for k in want[t]:
        if k in kv: r[t[:5] + '.' + k] = kv[k]
cols = [t[:5] + '.' + k for t in want for k in want[t]]
print('frame', *[c.split('.', 1)[1] for c in cols])
for f in sorted(rows):
    print(f, *[rows[f].get(c, '-') for c in cols])

# Second table: prepare against the bullet-batch submission counts the proxy
# logs (bolt_footprint: per-window admitted bullet-batch draws = 0x004bfd40
# calls, bullet instances, the footprint's draw-time us; `locks` there is the
# proxy's own replacement buffer), with Pearson r over the windows.
bf = {}
for line in open(log, errors='replace'):
    if not line.startswith('bolt_footprint '): continue
    kv = dict(re.findall(r'(\w+)=(\S+)', line))
    f = (int(kv['frame']) + 1) // 300 * 300 - 300  # frame=1199 closes the window logged as 900..1199
    fr = int(kv['frames']) or 1
    bf[f] = {k: float(kv[k]) / fr for k in ('draws', 'locks', 'instances')}
    bf[f]['bf_us'] = float(kv['us']) / fr
def pear(x, y):
    n = len(x); mx = sum(x) / n; my = sum(y) / n
    sxy = sum((a - mx) * (b - my) for a, b in zip(x, y))
    sx = sum((a - mx) ** 2 for a in x) ** .5; sy = sum((b - my) ** 2 for b in y) ** .5
    return sxy / (sx * sy) if sx and sy else float('nan')
print('\nwindow_end prepare_us bolt_draws/f locks/f instances/f bolt_footprint_us/f')
xs = {k: [] for k in ('draws', 'locks', 'instances', 'bf_us')}; ys = []
for f in sorted(rows):
    p = rows[f].get('resid.prepare_p50_us')
    # residual row frame=F covers frames F-299..F; bolt row frame=F-1 covers the same frames
    b = bf.get(f - 300)
    if p is None or b is None: continue
    print(f, p, *('%.2f' % b[k] for k in ('draws', 'locks', 'instances', 'bf_us')))
    ys.append(float(p))
    for k in xs: xs[k].append(b[k])
for k in xs: print('r(prepare,%s)=%.3f n=%d' % (k, pear(xs[k], ys), len(ys)))
