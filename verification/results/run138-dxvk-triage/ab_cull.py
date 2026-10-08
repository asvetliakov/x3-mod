#!/usr/bin/env python3
"""Run 137 A triage: in-flight frame time per run (qpc-based dt, 10 MHz), hitches, frame_phases window medians,
draws/issued bins, and (cull runs) frame time conditioned on the occlusion_cull row of the same frame.
In flight = load frame (largest dt) + 10 .. last frame - 10.  usage: ab_cull.py RUNDIR [RUNDIR...]"""
import re, sys, glob, statistics as st
from collections import defaultdict
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
def q(v, p): v = sorted(v); return v[min(len(v) - 1, int(len(v) * p))]
def summ(v): return f'n={len(v):5d} p50={st.median(v):6.2f} p95={q(v,.95):6.2f} mean={st.mean(v):6.2f}' if v else 'n=0'
for folder in sys.argv[1:]:
    log = glob.glob(f'{folder}/session-*.log')[0]
    fe = {}; oc = {}; fp = []
    for raw in open(log, 'rb'):
        k = raw.split(b' ', 1)[0]
        if k == b'frame_end': d = {a: float(b) for a, b in KV.findall(raw.decode('latin1'))}; fe[int(d['frame'])] = d
        elif k == b'occlusion_cull': d = {a: float(b) for a, b in KV.findall(raw.decode('latin1'))}; oc[int(d['frame'])] = d
        elif k == b'frame_phases': fp.append({a: float(b) for a, b in KV.findall(raw.decode('latin1'))})
    fs = sorted(fe)
    for a, b in zip(fs, fs[1:]):
        if b == a + 1: fe[b]['dtq'] = (fe[b]['qpc'] - fe[a]['qpc']) / 1e4  # ms
    L = max(fs, key=lambda f: fe[f]['dt_ms']); lo, hi = L + 10, fs[-1] - 10
    S = [f for f in fs if lo <= f <= hi and 'dtq' in fe[f]]
    dt = [fe[f]['dtq'] for f in S]
    print(f'### {folder} load_frame={L} inflight={lo}-{hi} frames={len(S)} secs={sum(dt)/1000:.0f}')
    print(' dt_ms', summ(dt), ' fps(mean)=%.1f' % (1000 / st.mean(dt)))
    print(' hitches', ' '.join(f'>{t}ms={sum(x > t for x in dt)}' for t in (33, 50, 100)))
    W = [r for r in fp if lo + 300 <= r['frame'] <= hi]
    cols = [c for c in W[0] if c.endswith('_p50_us') or c.endswith('_p95_us')] if W else []
    print(' frame_phases windows', len(W), ' '.join(f'{c[:-3]}={st.median([r[c] for r in W]):.0f}' for c in cols
          if not any(c.startswith(x) for x in ('prologue', 'scene_update', 'text', 'overlays'))))
    for key in ('draws', 'issued'):
        bins = defaultdict(list)
        for f in S: bins[int(fe[f][key]) // 20 * 20].append(fe[f]['dtq'])
        print(f' by_{key}:', ' | '.join(f'{b}:{len(v)} p50={st.median(v):.2f} mean={st.mean(v):.2f}' for b, v in sorted(bins.items()) if len(v) >= 20))
    if not oc: continue
    O = [f for f in S if f in oc]
    print(f' cull rows inflight={len(O)} of {len(S)}; candidates>0: {sum(oc[f]["candidates"] > 0 for f in O)}')
    for c in ('candidates', 'tested', 'hidden', 'skipped', 'drawn_late', 'unstable', 'no_hull', 'not_ready', 'ready_lag2', 'pool_truncated', 'errors', 'failed', 'refused', 'no_bounds', 'unbounded', 'state', 'blocks', 'test_us', 'retest_skipped', 'forced', 'stale', 'cadence'):
        v = [oc[f].get(c, 0) for f in O]; nz = [x for x in v if x]
        p95=q(v,.95) if v else 0; print(f'  {c:14s} p95_all={p95:5.0f} sum={sum(v):9.0f} max={max(v):5.0f} median_all={st.median(v):5.0f} median_nonzero={st.median(nz) if nz else 0:5.0f} frames_nonzero={len(nz)}')
    # ready_age (a,b,c) is not a float field: re-read
    ra = [0, 0, 0]
    for raw in open(log, 'rb'):
        if raw.startswith(b'occlusion_cull '):
            m = re.search(rb'frame=(\d+) .*ready_age=(\d+),(\d+),(\d+)', raw)
            if m and lo <= int(m.group(1)) <= hi:
                for i in range(3): ra[i] += int(m.group(i + 2))
    print(f'  ready_age inflight age1={ra[0]} age2={ra[1]} none={ra[2]}')
    g0 = [fe[f]['dtq'] for f in S if f not in oc or oc[f]['tested'] == 0]
    g1 = [fe[f]['dtq'] for f in O if oc[f]['tested'] > 0 and oc[f]['skipped'] == 0]
    g2 = [fe[f]['dtq'] for f in O if oc[f]['skipped'] > 0]
    print('  dt tested=0          ', summ(g0)); print('  dt tested>0 skipped=0', summ(g1)); print('  dt skipped>0        ', summ(g2))
    tb = defaultdict(list)
    for f in S:
        t = oc[f]['tested'] if f in oc else 0
        tb[0 if t == 0 else int(t) // 25 * 25 + 1].append(f)
    print('  by tested (bin start; 1=1..24):')
    for b, fl in sorted(tb.items()):
        if len(fl) < 10: continue
        print(f'   tested~{b:4d} n={len(fl):5d} dt_p50={st.median([fe[f]["dtq"] for f in fl]):6.2f} dt_mean={st.mean([fe[f]["dtq"] for f in fl]):6.2f}'
              f' draws_p50={st.median([fe[f]["draws"] for f in fl]):5.0f} issued_p50={st.median([fe[f]["issued"] for f in fl]):5.0f}'
              f' skipped_p50={st.median([oc[f]["skipped"] if f in oc else 0 for f in fl]):4.0f} visible_tested_p50={st.median([oc[f]["tested"]-oc[f]["skipped"] if f in oc else 0 for f in fl]):4.0f}')
