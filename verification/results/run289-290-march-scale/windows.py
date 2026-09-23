"""run289/290: per 300-frame gpu_sync window pass medians (ms), repair census, camera motion.
usage: windows.py <session.log>"""
import re, sys, math, collections
F = 0.264  # per-pass query floor, run280 note
W = collections.defaultdict(dict); dt = {}; fr = {}; cen = {}; cam = {}
prev = None
for line in open(sys.argv[1], errors='replace'):
    if line.startswith('gpu_sync_timing window='):
        d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
        w = int(d['window']); W[w][d['pass']] = (int(d['median_us'])/1e3, int(d['p90_us'])/1e3, int(d['wait_median_us'])/1e3, int(d['n']))
        dt[w] = (int(d['dt_median_us'])/1e3, int(d['dt_p90_us'])/1e3); fr[w] = d['frames']
    elif line.startswith('volumetric_fog_repair_census window='):
        d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
        cen[int(d['window'])] = (d['median_ppm'], d['needs_px'], d['needs_p90_px'], d['needs_max_px'], d['needs_scale'], d['needs_missed'])
    elif line.startswith('camera_state device='):
        f = int(re.search(r' frame=(\d+)', line).group(1))
        if re.search(r' valid=(\d)', line).group(1) != '1': prev = None; continue
        t = tuple(map(float, re.search(r' t=([-\d.e]+),([-\d.e]+),([-\d.e]+)', line).groups()))
        R = [float(re.search(r' r%d%d=([-\d.e]+)' % (i, j), line).group(1)) for i in range(3) for j in range(3)]
        c = cam.setdefault(f // 300 + 1, {'rot': [], 'step': []})
        if prev:
            tr = sum(R[k]*prev[1][k] for k in range(9))
            c['rot'].append(math.degrees(math.acos(max(-1, min(1, (tr-1)/2))))); c['step'].append(math.dist(t, prev[0]))
        prev = (t, R)
def q(a, p):
    a = sorted(a); return a[min(len(a)-1, int(p*len(a)))] if a else float('nan')
K = ['scene','engine','fog_route','fog_march','fog_composite','fog_repair','motes','taa','taa_copy','taa_mask','taa_mask_tests','taa_mask_x','taa_mask_y','taa_box','taa_resolve','taa_display','bloom','present']
print('win frames dt50 dt90 | ' + ' '.join(K) + ' | npass sum_of_leaf_and_top_medians(n>=150, excl. taa/fog_route/taa_mask_* nests; scene included) | rot50 rot90 still% | census(ppm,needs_px,p90,max,scale,missed)')
for w in sorted(W):
    p = W[w]; g = lambda k: p[k][0] if k in p else float('nan')
    s = sum(v[0] for k, v in p.items() if k not in ('taa','fog_route','taa_mask_tests','taa_mask_x','taa_mask_y') and v[3] >= 150)
    c = cam.get(w, {'rot': [], 'step': []})
    still = 100*sum(1 for r in c['rot'] if r < 0.01)/max(1, len(c['rot']))
    print(f"W{w} {fr[w]} {dt[w][0]:.1f} {dt[w][1]:.1f} | " + ' '.join(f"{g(k):.2f}" for k in K) +
          f" | {len(p)} {s:.1f} | {q(c['rot'],.5):.3f} {q(c['rot'],.9):.3f} {still:.0f} | {cen.get(w)}")
print('n per pass in last window:', {k: v[3] for k, v in W[max(W)].items()})
