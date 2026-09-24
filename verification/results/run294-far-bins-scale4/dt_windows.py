"""run294 (scale 4, far bins 24, taa-debug, no gpu_sync) vs run290 (bins 40, gpu_sync) / run291 (bins 40, taa-debug):
per-300-frame frame_timing dt p50/p95 (ms), fog-applied frames per window, option lines. usage: dt_windows.py"""
import re, collections
RUNS = {'run290 b40 sync': '/tmp/x3-bottleX3-run290/session-20260924-031933-212.log',
        'run291 b40 dbg': '/tmp/x3-bottleX3-run291/session-20260924-034548-212.log',
        'run294 b24 dbg': '/tmp/x3-bottleX3-run294/session-20260924-040818-212.log'}
for name, log in RUNS.items():
    opt = {}; rows = []; fog = collections.Counter()
    for line in open(log, errors='replace'):
        if line.startswith('proxy_options'):
            opt = {k: v for k, v in (t.split('=', 1) for t in line.split()[1:] if '=' in t)}
        elif line.startswith('frame_timing '):
            d = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
            rows.append((int(d['frame']), int(d['dt_p50_us'])/1e3, int(d['dt_p95_us'])/1e3))
        elif line.startswith('volumetric_fog_cards '):
            m = re.search(r' frame=(\d+).* applied=(\d+)', line)
            if m and int(m.group(2)) > 0: fog[(int(m.group(1))-1)//300] += 1
        elif line.startswith('volumetric_fog_far_bins'): fb = line.split()[1]
    print(f"== {name}: {fb} scale={opt.get('X3M_FOG_MARCH_SCALE')} gpu_sync={opt.get('X3M_GPU_SYNC_TIMING')} taa_debug={opt.get('X3M_TAA_DEBUG')} res={opt.get('X3M_RESOLUTION', '?')}")
    for f, p50, p95 in rows:
        print(f"  frame {f:6d} dt p50 {p50:6.1f} p95 {p95:6.1f} fog_applied_frames {fog[(f-1)//300]}")
# aggregate: median of per-window p50/p95 over full-fog windows (300 applied frames), frames 900..4200 for the two taa-debug runs
import statistics as st
for name in ('run291 b40 dbg', 'run294 b24 dbg'):
    r = [l.split() for l in open(RUNS[name], errors='replace') if l.startswith('frame_timing ')]
    v = [(int(d['dt_p50_us'])/1e3, int(d['dt_p95_us'])/1e3) for d in (dict(t.split('=', 1) for t in x[1:] if '=' in t) for x in r) if 900 <= int(d['frame']) <= 4200]
    print(f"AGG {name}: n_win={len(v)} median dt p50 {st.median(a for a, b in v):.2f} p95 {st.median(b for a, b in v):.2f} ms")
