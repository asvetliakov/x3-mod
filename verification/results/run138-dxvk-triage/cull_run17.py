#!/usr/bin/env python3
"""Run 138 A: batched-cull activity on in-flight frames with candidates>0: median/p95/max per field, phase spread,
and per-frame cost (test_us) vs saving (skipped x 10 us).  usage: cull_run17.py RUNDIR"""
import re, sys, glob, statistics as st
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
def q(v, p): v = sorted(v); return v[min(len(v) - 1, int(len(v) * p))]
d = sys.argv[1]; fe = {}; oc = {}; ps = {}
for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
    k = raw.split(b' ', 1)[0]
    if k in (b'frame_end', b'occlusion_cull'):
        s = raw.decode('latin1'); x = {a: float(b) for a, b in KV.findall(s)}; f = int(x['frame'])
        if k == b'frame_end': fe[f] = x
        else:
            oc[f] = x; m = re.search(r'retest_phase_spread=(\d+),(\d+)', s); ps[f] = (int(m.group(1)), int(m.group(2))) if m else None
fs = sorted(fe); L = max(fs, key=lambda f: fe[f]['dt_ms']); lo, hi = L + 10, fs[-1] - 10
O = [f for f in oc if lo <= f <= hi]; C = [f for f in O if oc[f]['candidates'] > 0]
print(f'inflight cull rows={len(O)} candidates>0={len(C)} tested>0={sum(oc[f]["tested"]>0 for f in O)} skipped>0={sum(oc[f]["skipped"]>0 for f in O)}')
for c in ('candidates','tested','hidden','skipped','blocks','test_us','retest_skipped','forced','stale','drawn_late','no_hull','pool_truncated','errors','failed','refused','unstable','not_ready','ready_lag2','cadence'):
    v = [oc[f][c] for f in C]
    print(f' {c:15s} median={st.median(v):7.1f} p95={q(v,.95):7.0f} max={max(v):7.0f} sum={sum(v):9.0f} nonzero={sum(1 for x in v if x)}')
a = [ps[f][0] for f in C if ps[f]]; b = [ps[f][1] for f in C if ps[f]]
print(f' phase_spread first: median={st.median(a)} max={max(a)}  second: median={st.median(b)} max={max(b)}')
ra = [0, 0, 0]
for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
    if raw.startswith(b'occlusion_cull '):
        m = re.search(rb'frame=(\d+) .*ready_age=(\d+),(\d+),(\d+)', raw)
        if m and lo <= int(m.group(1)) <= hi:
            for i in range(3): ra[i] += int(m.group(i + 2))
print(f' ready_age age1={ra[0]} age2={ra[1]} none={ra[2]}')
cost = [oc[f]['test_us'] for f in C]; save = [oc[f]['skipped'] * 10 for f in C]; net = [s - c for s, c in zip(save, cost)]
print(f' per frame (cand>0): test_us median={st.median(cost):.0f} mean={st.mean(cost):.0f}; skipped*10us median={st.median(save):.0f} mean={st.mean(save):.0f}; net mean={st.mean(net):.0f} us; frames net>0={sum(x>0 for x in net)}/{len(net)}')
T = [f for f in C if oc[f]['tested'] > 0]
print(f' us per tested (sum test_us / sum tested over tested>0 frames)={sum(oc[f]["test_us"] for f in T)/max(1,sum(oc[f]["tested"] for f in T)):.2f}; per block={sum(oc[f]["test_us"] for f in T)/max(1,sum(oc[f]["blocks"] for f in T)):.2f}')
print(f' draws-issued (cand>0) median={st.median([fe[f]["draws"]-fe[f]["issued"] for f in C if f in fe])} mean={st.mean([fe[f]["draws"]-fe[f]["issued"] for f in C if f in fe]):.1f}; skipped mean={st.mean([oc[f]["skipped"] for f in C]):.1f}')
