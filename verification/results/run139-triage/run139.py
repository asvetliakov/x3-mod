#!/usr/bin/env python3
"""Run 139 A triage (run20 engine, run21 off). usage: run139.py RUNDIR...
Q1 scene-matched dt bins (scene = app draws + engine_skipped_draws), Q2 engine skip fields, Q3 engine light hold, Q4 >100ms."""
import re, sys, glob, statistics as st, collections as C
KV = re.compile(r'(\w+)=(-?[0-9.]+(?:,-?[0-9.]+)*)(?=\s|$)')
def pc(v, q): v = sorted(v); return v[min(len(v)-1, int(q*len(v)))] if v else 0
for d in sys.argv[1:]:
    fe, oc, el = {}, {}, {}; prev = None; slow = []; samp = 0; samp_ex = None; cfg = []
    for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
        k = raw.split(b' ', 1)[0]
        if k in (b'frame_end', b'occlusion_cull', b'engine_light_frame'):
            x = dict(KV.findall(raw.decode('latin1'))); f = int(x.get('frame', -1))
            {b'frame_end': fe, b'occlusion_cull': oc, b'engine_light_frame': el}[k][f] = x
            if k == b'frame_end' and float(x['dt_ms']) > 100: slow.append((f, x['dt_ms'], prev))
        elif k == b'occlusion_engine_sample':
            samp += 1; samp_ex = samp_ex or raw[:200].decode('latin1')
        elif k in (b'occlusion_engine_cull', b'occlusion_cull_config', b'engine_light_mode'): cfg.append(raw[:260].decode('latin1').strip())
        if k != b'frame_end' and k != b'occlusion_engine_sample': prev = raw[:120].decode('latin1').strip()
    fs = sorted(f for f in fe if f > 0); load = max(fs, key=lambda f: float(fe[f]['dt_ms'])); inf = [f for f in fs if load + 10 <= f <= fs[-1] - 10]
    print(f'### {d} load={load} inflight={inf[0]}-{inf[-1]} n={len(inf)}'); [print(' cfg', c) for c in cfg]
    g = lambda f, k: int(oc.get(f, {}).get(k, 0))
    bins = C.defaultdict(list)
    for f in inf:
        sc = int(fe[f]['draws']) + g(f, 'engine_skipped_draws'); bins[sc // 20 * 20].append(float(fe[f]['dt_ms']))
    print(' Q1 dt by scene draws (draws+engine_skipped_draws):', ' | '.join(f'{b}:{len(v)} p50={st.median(v):.2f} p95={pc(v,.95):.2f}' for b, v in sorted(bins.items()) if len(v) >= 50))
    # 300-frame windows: mean dt, scene draws, engine skipped
    print(' Q1 windows(300): start mean_scene mean_draws mean_eskip dt_p50')
    for s in range(inf[0] // 300 * 300, inf[-1], 300):
        w = [f for f in inf if s <= f < s + 300]
        if len(w) < 250: continue
        print(f'  {s:6d} scene={st.mean(int(fe[f]["draws"]) + g(f,"engine_skipped_draws") for f in w):6.1f} draws={st.mean(int(fe[f]["draws"]) for f in w):6.1f} eskip={st.mean(g(f,"engine_skipped_draws") for f in w):5.1f} dt_p50={st.median(float(fe[f]["dt_ms"]) for f in w):6.2f}')
    for key in ('engine_published', 'engine_skipped_parts', 'engine_skipped_draws', 'withheld', 'engine_visits', 'engine_view_changes', 'engine_partial', 'engine_no_position', 'engine_overflow', 'engine_dropped', 'engine_unarmed'):
        v = [g(f, key) for f in inf if f in oc]
        if any(v): print(f' Q2 {key:22s} nz_frames={sum(x>0 for x in v)}/{len(v)} p50={pc(v,.5)} p95={pc(v,.95)} max={max(v)} sum={sum(v)}')
    gr = [[0,0,0]] + [list(map(int, oc[f].get('guard_rejected', '0,0,0').split(','))) for f in inf if f in oc]
    print(' Q2 guard_rejected model,stamp,position sums=', [sum(r[i] for r in gr) for i in range(3)], 'nz_frames=', sum(any(r) for r in gr), 'max=', [max(r[i] for r in gr) for i in range(3)])
    print(f' Q2 occlusion_engine_sample rows={samp} first={samp_ex}')
    h = lambda f, k: int(el.get(f, {}).get(k, 0))
    for key in ('held', 'hold_expired', 'ships', 'ships_drawn', 'lights', 'ships_dropped'):
        v = [h(f, key) for f in inf if f in el]
        print(f' Q3 {key:13s} nz={sum(x>0 for x in v)}/{len(v)} p50={pc(v,.5)} p95={pc(v,.95)} max={max(v)} hist={sorted(C.Counter(v).items())[:12]}')
    drops = [f for f in inf if f in el and f-1 in el and h(f, 'lights') < h(f-1, 'lights')]
    print(f' Q3 frames with lights drop={len(drops)}, with held rising at the drop={sum(h(f,"held")>h(f-1,"held") for f in drops)}, held>0 at drop={sum(h(f,"held")>0 for f in drops)}')
    sd = [f for f in inf if f in el and f-1 in el and h(f, 'ships_drawn') < h(f-1, 'ships_drawn')]
    print(f' Q3 frames ships_drawn drop={len(sd)} of which lights also drop={sum(f in set(drops) for f in sd)} held rise={sum(h(f,"held")>h(f-1,"held") for f in sd)}')
    for f in drops[:: max(1, len(drops)//3)][:3]:
        print('  ex', ' ; '.join(f'f{g2} ships={h(g2,"ships")} drawn={h(g2,"ships_drawn")} lights={h(g2,"lights")} held={h(g2,"held")} exp={h(g2,"hold_expired")} plates={el[g2].get("plates")}' for g2 in range(f-2, f+3) if g2 in el))
    print(' Q4 dt>100ms:'); [print(f'  frame={f} dt={dt} prev={p}') for f, dt, p in slow]
# --- part 2: qpc dt (float ms) by scene draws, view_submit per frame_phases window, lights-drop records context
for d in sys.argv[1:]:
    fe, oc, el, fp = {}, {}, {}, []
    for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
        k = raw.split(b' ', 1)[0]
        if k in (b'frame_end', b'occlusion_cull', b'engine_light_frame'):
            x = dict(KV.findall(raw.decode('latin1'))); {b'frame_end': fe, b'occlusion_cull': oc, b'engine_light_frame': el}[k][int(x['frame'])] = x
        elif k == b'frame_phases': fp.append(dict(KV.findall(raw.decode('latin1'))))
    fs = sorted(f for f in fe if f > 0); load = max(fs, key=lambda f: float(fe[f]['dt_ms'])); inf = [f for f in fs if load + 10 <= f <= fs[-1] - 10 and f - 1 in fe]
    dq = {f: (int(fe[f]['qpc']) - int(fe[f-1]['qpc'])) / 1e4 for f in inf}
    g = lambda f, k: int(oc.get(f, {}).get(k, 0)); sc = lambda f: int(fe[f]['draws']) + g(f, 'engine_skipped_draws')
    bins = C.defaultdict(list)
    for f in inf: bins[sc(f) // 20 * 20].append(dq[f])
    print(f'### {d} P2 qpc dt by scene draws:', ' | '.join(f'{b}:{len(v)} p50={st.median(v):.2f} p95={pc(v,.95):.2f}' for b, v in sorted(bins.items()) if len(v) >= 50))
    print('  all: p50=%.2f p95=%.2f mean=%.2f' % (st.median(dq.values()), pc(list(dq.values()), .95), st.mean(dq.values())))
    print('  frame_phases windows: end scene eskip dt_p50 dt_p95 view_submit_p50 view_submit_p95')
    for w in fp:
        e = int(w['frame']); r = [f for f in range(e - 299, e + 1) if f in dq]
        if len(r) >= 250: print(f'   {e:6d} scene={st.mean(sc(f) for f in r):6.1f} eskip={st.mean(g(f,"engine_skipped_draws") for f in r):5.1f} dt={w["dt_p50_us"]}/{w["dt_p95_us"]} vs={w["view_submit_p50_us"]}/{w["view_submit_p95_us"]}')
    h = lambda f, k: int(el.get(f, {}).get(k, 0))
    drops = [f for f in inf if f in el and f-1 in el and h(f, 'lights') < h(f-1, 'lights')]
    kinds = C.Counter((h(f,'ships') - h(f-1,'ships'), h(f,'main') - h(f-1,'main'), h(f,'records') - h(f-1,'records')) for f in drops)
    print('  lights drops (dships,dmain,drecords):', sorted(kinds.items()))
    print('  first 12 drops:', [(f, h(f-1,'lights'), h(f,'lights'), h(f-1,'main'), h(f,'main'), h(f,'ships'), h(f,'held')) for f in drops[:12]])
    hs = [f for f in inf if f in el and h(f,'held') > 0]
    runs = []; 
    for f in hs:
        if runs and f == runs[-1][1] + 1: runs[-1][1] = f
        else: runs.append([f, f])
    print(f'  held>0 runs={len(runs)} lengths={sorted(C.Counter(b-a+1 for a,b in runs).items())} first={runs[:8]}')
    print('  hold_expired frames:', [f for f in inf if h(f,'hold_expired') > 0])
