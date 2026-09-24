"""Frame timing per session: per-300-frame frame_timing dt p50/p95 (ms); aggregate = median over windows whose 300 frames
all applied fog (volumetric_fog_cards applied>0), and over all windows except the first. run291/run294 recomputed the same
way for the baseline (run294-far-bins-scale4 used frames 900..4200). usage: dt_windows.py RUN..."""
import re, sys, glob, collections, statistics as st
for run in sys.argv[1:]:
    log = sorted(glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log'))[0]; rows = []; fog = collections.Counter(); opt = {}
    for line in open(log, errors='replace'):
        if line.startswith('frame_timing qpc'):
            d = dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
            rows.append((int(d['frame']), int(d['dt_p50_us'])/1e3, int(d['dt_p95_us'])/1e3))
        elif line.startswith('volumetric_fog_cards '):
            m = re.search(r' frame=(\d+).* applied=(\d+)', line)
            if m and int(m.group(2)) > 0: fog[(int(m.group(1))-1)//300] += 1
        elif line.startswith('proxy_options'):
            opt = {k: v for k, v in (t.split('=', 1) for t in line.split()[1:] if '=' in t)}
    full = [(a, b) for f, a, b in rows if fog[(f-1)//300] == 300]; rest = [(a, b) for f, a, b in rows[1:]]
    agg = lambda v: f"n={len(v)} p50 {st.median(a for a, b in v):.2f} p95 {st.median(b for a, b in v):.2f}" if v else 'n=0'
    print(f"run{run} dither={opt.get('X3M_HDR_DITHER')} taa_debug={opt.get('X3M_TAA_DEBUG')} gpu_sync={opt.get('X3M_GPU_SYNC_TIMING')} hold={opt.get('X3M_TAA_REGION_HOLD')} "
          f"| full-fog windows {agg(full)} | all windows excl first {agg(rest)} ms")
