#!/usr/bin/env python3
"""Run 140 A triage. usage: run140.py RUNDIR... (run21 ref, run22 flight1).
Q1 dt by draws (frame_end, capture=0, in flight), frame_phases windows binned by window mean draws;
Q2 engine_stage node_* / stage_us / discs, engine_light_frame held; Q3 rows/frame by kind, >100 ms stalls."""
import re, sys, glob, statistics as st, collections as C
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
def pc(v, q): v = sorted(v); return v[min(len(v)-1, int(q*len(v)))] if v else 0
for d in sys.argv[1:]:
    fe, es, el, fp = {}, {}, {}, []; kinds = C.Counter(); prev = []; slow = []
    for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
        k = raw.split(b' ', 1)[0]; kinds[k] += 1
        if k in (b'frame_end', b'engine_stage', b'engine_light_frame', b'frame_phases'):
            x = dict(KV.findall(raw.decode('latin1')))
            if k == b'frame_phases': fp.append(x); continue
            f = int(x.get('frame', -1)); {b'frame_end': fe, b'engine_stage': es, b'engine_light_frame': el}[k][f] = x
            if k == b'frame_end' and float(x['dt_ms']) > 100: slow.append((f, x['dt_ms'], x.get('capture'), list(prev)))
        if k != b'frame_end': prev = (prev + [raw[:90].decode('latin1').strip()])[-3:]
    fs = sorted(f for f in fe if f > 0); load = max(fs, key=lambda f: float(fe[f]['dt_ms']) if fe[f].get('capture') == '0' else 0)
    inf = [f for f in fs if load + 10 <= f <= fs[-1] - 10 and fe[f].get('capture') == '0']
    print(f'### {d} load={load} inflight={inf[0]}-{inf[-1]} n={len(inf)} capture_frames={sum(fe[f].get("capture")!="0" for f in fs)}')
    dt = [float(fe[f]['dt_ms']) for f in inf]; dr = [int(fe[f]['draws']) for f in inf]
    print(f' Q1 dt p50={pc(dt,.5)} p95={pc(dt,.95)} mean={st.mean(dt):.2f} draws p50={pc(dr,.5)} mean={st.mean(dr):.1f}')
    bins = C.defaultdict(list)
    for f in inf: bins[int(fe[f]['draws']) // 20 * 20].append(float(fe[f]['dt_ms']))
    print(' Q1 dt by draws:', ' | '.join(f'{b}:{len(v)} p50={st.median(v):.2f} p95={pc(v,.95):.2f}' for b, v in sorted(bins.items()) if len(v) >= 50))
    # frame_phases windows within flight; bin by mean draws of the window
    ph = ('dt', 'pre_render', 'views', 'view_setup', 'view_submit', 'present')
    win = [w for w in fp if inf[0] + 300 <= int(w['frame']) <= inf[-1]]
    for w in win:
        fr = [int(fe[f]['draws']) for f in range(int(w['frame']) - 300, int(w['frame'])) if f in fe]; w['md'] = st.mean(fr) if fr else 0
    print(f' Q1 frame_phases windows n={len(win)} medians of window p50/p95 (us):', ' '.join(f'{p}={st.median(float(w[p+"_p50_us"]) for w in win):.0f}/{st.median(float(w[p+"_p95_us"]) for w in win):.0f}' for p in ph))
    wb = C.defaultdict(list)
    for w in win: wb[int(w['md']) // 40 * 40].append(w)
    for b, ws in sorted(wb.items()):
        if len(ws) >= 3: print(f'  draws~{b}: n={len(ws)} ' + ' '.join(f'{p}={st.median(float(w[p+"_p50_us"]) for w in ws):.0f}' for p in ph))
    g = lambda m, f, k: float(m.get(f, {}).get(k, 0))
    for key in ('node_walk_us', 'node_roots', 'node_walked', 'node_records', 'node_dupes', 'node_root_overflow', 'node_not_ship', 'node_invalid', 'node_hidden', 'node_overflow', 'node_children', 'records', 'nozzles', 'discs', 'calls', 'stage_us', 'ribbons_live'):
        v = [g(es, f, key) for f in inf if f in es and key in es[f]]
        if v: print(f' Q2 {key:18s} n={len(v)} nz={sum(x>0 for x in v)} p50={pc(v,.5)} p95={pc(v,.95)} max={max(v)} mean={st.mean(v):.2f}')
    ran = [f for f in inf if g(es, f, 'ran') > 0]
    print(f' Q2 frames ran={len(ran)}/{len(inf)}; stage_us on ran frames p50={pc([g(es,f,"stage_us") for f in ran],.5)} p95={pc([g(es,f,"stage_us") for f in ran],.95)}')
    for key in ('held', 'hold_expired', 'hold_walked', 'lights', 'ships_drawn'):
        v = [g(el, f, key) for f in inf if f in el]
        print(f' Q2 light {key:12s} nz={sum(x>0 for x in v)}/{len(v)} p50={pc(v,.5)} p95={pc(v,.95)} max={max(v)}')
    n = len(fs)
    print(' Q3 top kinds rows/frame:', ', '.join(f'{k.decode()}={c/n:.2f}' for k, c in kinds.most_common(14)))
    print(f' Q4 stalls >100ms: {len(slow)}')
    for s in slow: print(f'  frame={s[0]} dt={s[1]} capture={s[2]} prev={s[3][-1][:80] if s[3] else ""}')
