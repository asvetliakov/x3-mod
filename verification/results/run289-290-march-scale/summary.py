"""Aggregate of the 300-frame gpu_sync windows (run289 scale 2, run290 scale 4): median over full fog windows
(every pass n >= 200) of the per-window medians, split still (>= 50 % frames with rotation < 0.01 deg) / turning;
top-level proxy GPU (sum of wait_median, the GPU work pending after submission) for the GPU-bound question.
usage: summary.py"""
import re, math, statistics as st, collections
RUNS = {'run289 s2': '/tmp/x3-bottleX3-run289/session-20260924-031454-212.log', 'run290 s4': '/tmp/x3-bottleX3-run290/session-20260924-031933-212.log'}
TOP = ['engine', 'shadow_depth', 'sun_apply', 'retention', 'fog_route', 'taa', 'hdr_writeback', 'bloom']
# leaf passes (no nested pair): their wait_median ~ their median, i.e. GPU execution pending after submission
LEAF = ['engine', 'shadow_depth', 'sun_apply', 'retention', 'fog_march', 'fog_composite', 'fog_repair', 'motes', 'taa_copy', 'taa_mask_tests',
        'taa_mask_x', 'taa_mask_y', 'taa_box', 'taa_resolve', 'hdr_writeback', 'bloom', 'present']
K = ['scene', 'engine', 'fog_route', 'fog_march', 'fog_composite', 'fog_repair', 'motes', 'taa', 'taa_copy', 'taa_mask', 'taa_mask_tests',
     'taa_mask_x', 'taa_mask_y', 'taa_box', 'taa_resolve', 'bloom', 'hdr_writeback', 'present']
for name, log in RUNS.items():
    W = collections.defaultdict(dict); dt = {}; cen = {}; cam = {}; prev = None
    for line in open(log, errors='replace'):
        if line.startswith('gpu_sync_timing window='):
            d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv); w = int(d['window'])
            W[w][d['pass']] = (int(d['median_us'])/1e3, int(d['p90_us'])/1e3, int(d['wait_median_us'])/1e3, int(d['n'])); dt[w] = (int(d['dt_median_us'])/1e3, int(d['dt_p90_us'])/1e3)
        elif line.startswith('volumetric_fog_repair_census window='):
            d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv); cen[int(d['window'])] = (int(d['n']), int(d['needs_px']), int(d['median_ppm']))
        elif line.startswith('camera_state device='):
            f = int(re.search(r' frame=(\d+)', line).group(1))
            if re.search(r' valid=(\d)', line).group(1) != '1': prev = None; continue
            R = [float(re.search(r' r%d%d=([-\d.e]+)' % (i, j), line).group(1)) for i in range(3) for j in range(3)]
            if prev: cam.setdefault(f // 300 + 1, []).append(math.degrees(math.acos(max(-1, min(1, (sum(a*b for a, b in zip(R, prev))-1)/2)))))
            prev = R
    full = [w for w in W if all(W[w].get(k, (0, 0, 0, 0))[3] >= 200 for k in ('fog_march', 'taa', 'engine'))]
    still = [w for w in full if sum(r < .01 for r in cam.get(w, [])) >= .5*len(cam.get(w, [1]))]
    groups = {'all': full, 'still': still, 'turning': [w for w in full if w not in still]}
    print(f'== {name}: full fog windows {min(full)}..{max(full)} ({len(full)}), still windows {still}')
    for g, ws in groups.items():
        if not ws: continue
        med = lambda k, i=0: st.median(W[w][k][i] for w in ws if k in W[w])
        print(f'  [{g}] n_win={len(ws)} dt p50 {st.median(dt[w][0] for w in ws):.1f} p90 {st.median(dt[w][1] for w in ws):.1f} ms | ' + ' '.join(f'{k}={med(k):.2f}/{med(k,1):.2f}' for k in K))
        gpu = st.median(sum(W[w][k][2] for k in LEAF if k in W[w]) for w in ws); ser = st.median(sum(W[w][k][0] for k in TOP if k in W[w]) for w in ws)
        print(f'    top-level proxy+engine serialised sum {ser:.1f} ms, leaf-pass GPU-pending (wait) sum, a lower bound of GPU time per frame {gpu:.1f} ms, scene+present {med("scene")+med("present"):.1f}')
    cs = [cen[w] for w in full if w in cen]
    print(f'  needs census per window (n, needs_px median/frame, repair ppm): ' + ' '.join(f'W{w}:{cen[w][1]}/{cen[w][2]}' for w in full if w in cen) +
          f' | median needs_px {st.median(c[1] for c in cs)} = {st.median(c[1] for c in cs)/7372800*1e6:.0f} ppm of 7,372,800')
