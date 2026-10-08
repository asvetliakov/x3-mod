#!/usr/bin/env python3
"""Run 137 A: (a) in-flight frames 33-50 ms (qpc dt) per run, clustered, with run16 cull fields;
(b) run16 within matched app-draw bins: dt by visible tests (tested - skipped) and by skipped.  usage: RUN16DIR RUN15DIR"""
import re, sys, glob, statistics as st
from collections import defaultdict
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
def load(folder):
    fe = {}; oc = {}
    for raw in open(glob.glob(f'{folder}/session-*.log')[0], 'rb'):
        k = raw.split(b' ', 1)[0]
        if k in (b'frame_end', b'occlusion_cull'):
            d = {a: float(b) for a, b in KV.findall(raw.decode('latin1'))}; (fe if k == b'frame_end' else oc)[int(d['frame'])] = d
    fs = sorted(fe)
    for a, b in zip(fs, fs[1:]):
        if b == a + 1: fe[b]['dtq'] = (fe[b]['qpc'] - fe[a]['qpc']) / 1e4
    L = max(fs, key=lambda f: fe[f]['dt_ms']); return fe, oc, [f for f in fs if L + 10 <= f <= fs[-1] - 10 and 'dtq' in fe[f]]
for name, d in zip(('run16', 'run15'), sys.argv[1:3]):
    fe, oc, S = load(d)
    mid = [f for f in S if 33 < fe[f]['dtq'] <= 50]
    cl = []
    for f in mid:
        if cl and f - cl[-1][-1] <= 3: cl[-1].append(f)
        else: cl.append([f])
    print(f'{name}: 33-50ms frames={len(mid)} clusters={len(cl)} 20-33ms frames={sum(20 < fe[f]["dtq"] <= 33 for f in S)}')
    for c in cl:
        o = [oc.get(f, {}) for f in c]
        print(f'  {c[0]}-{c[-1]} n={len(c)} dt_max={max(fe[f]["dtq"] for f in c):.1f} draws={fe[c[0]]["draws"]:.0f} ' +
              (f'tested={o[0].get("tested",0):.0f} skipped={o[0].get("skipped",0):.0f} not_ready={sum(x.get("not_ready",0) for x in o):.0f} drawn_late={sum(x.get("drawn_late",0) for x in o):.0f} unstable={sum(x.get("unstable",0) for x in o):.0f}' if oc else ''))
fe, oc, S = load(sys.argv[1])
S = [f for f in S if fe[f]['dtq'] < 33 and f in oc]
for lo in (160, 180, 200, 220, 240):
    sel = [f for f in S if lo <= fe[f]['draws'] < lo + 20]
    for key, fn in (('visible_tests', lambda f: oc[f]['tested'] - oc[f]['skipped']), ('skipped', lambda f: oc[f]['skipped'])):
        b = defaultdict(list)
        for f in sel: b[int(fn(f)) // 20 * 20].append(fe[f]['dtq'])
        print(f'draws {lo}-{lo+19} by {key}: ' + ' | '.join(f'{k}:{len(v)} p50={st.median(v):.2f}' for k, v in sorted(b.items()) if len(v) >= 30))
