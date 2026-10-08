#!/usr/bin/env python3
"""Run 140 A flight 2 (run23): window medians of loop/pass/residual/frame_timing rows (in-flight windows),
game_phase_segment per site on slow frames (frame dt 12..100 ms). usage: run23_phases.py RUNDIR"""
import sys, glob, re, statistics as st, collections as C
NAMES = 'LoopSetup Clock Pump Channels PendingVm Services Input Simulation Cockpits Render PostRender Detail Presentation Tail Exit'.split()
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)'); rows = C.defaultdict(list); seg = C.defaultdict(lambda: C.defaultdict(float)); slowf = {}; dt = {}
for raw in open(glob.glob(f'{sys.argv[1]}/session-*.log')[0], 'rb'):
    k = raw.split(b' ', 1)[0].decode('latin1')
    if k in ('loop_phases', 'pass_phases', 'residual_phases', 'frame_timing', 'frame_phases', 'scene_graph_census', 'frame_end', 'game_phase_slow_frame', 'game_phase_segment'):
        s = raw.decode('latin1'); x = dict(KV.findall(s))
        if k == 'frame_end': dt[int(x['frame'])] = (float(x['dt_ms']), x.get('capture')); continue
        if k == 'game_phase_slow_frame': slowf[int(x['frame'])] = int(x['covered_ticks']); continue
        if k == 'game_phase_segment':
            nm = NAMES[int(x['phase'])] if int(x['phase']) < len(NAMES) else f'p{x["phase"]}'
            if nm == 'Input': nm += f'.part{x.get("input_part")}'
            seg[int(x['frame'])][nm] += (int(x['qpc_end']) - int(x['qpc_begin'])) / 10.0; continue
        x['raw'] = s; rows[k].append(x)
load = max(dt, key=lambda f: dt[f][0]); last = max(dt)
print(f'load frame={load} dt={dt[load][0]} last={last}')
for k, keys in (('loop_phases', 'collide simulate post passb cutevent containers sweep sum region input'.split()),
                ('pass_phases', 'apply draw end sum view_submit'.split()), ('residual_phases', 'prepare setup other'.split()),
                ('frame_phases', 'dt pre_render views view_setup view_submit present'.split())):
    w = [x for x in rows[k] if load + 300 <= int(x['frame']) <= last]
    print(f'{k} windows={len(w)}/{len(rows[k])}', ' '.join(f'{c}={st.median(float(x[c+"_p50_us"]) for x in w):.0f}' for c in keys if c + '_p50_us' in w[0]),
          ' passes_p50=' + str(st.median(float(x.get('passes_p50', 0)) for x in w)))
w = [x for x in rows['frame_timing'] if load + 300 <= int(x['frame']) <= last]
for c in ('dt_p50_us', 'draws_p50', 'draw_p50_us', 'draw_native_p50_us', 'scene_p50_us', 'state_calls_p50', 'gap_pre_p50_us', 'gap_draw_p50_us', 'gap_draw_per_draw_us', 'present_p50_us'):
    print(f' frame_timing {c} median={st.median(float(x[c]) for x in w):.1f} range={min(float(x[c]) for x in w):.1f}..{max(float(x[c]) for x in w):.1f}')
red = [re.search(r'state_redundant=(\d+),(\d+),(\d+)', x['raw']) for x in w]; shd = [re.search(r'state_shadowed=(\d+),(\d+),(\d+)', x['raw']) for x in w]
print(' state_redundant (3 fields, per frame median over windows):', [st.median(int(m.group(i)) / 300 for m in red) for i in (1, 2, 3)],
      ' state_shadowed:', [st.median(int(m.group(i)) / 300 for m in shd) for i in (1, 2, 3)])
print(' state_top first window:', re.search(r'state_top=(\S+)', w[0]['raw']).group(1), ' redundant_top:', re.search(r'redundant_top=(\S+)', w[0]['raw']).group(1))
sg = [x for x in rows['scene_graph_census'] if load <= int(x['frame'])]
if sg: print(' scene_graph_census last:', {k: v for k, v in sg[-1].items() if k != 'raw'})
fl = [f for f in slowf if f > load and dt.get(f, (0, '0'))[1] == '0' and 12 <= dt[f][0] < 100]
print(f'slow frames in flight (dt 12..100 ms) with tape={len(fl)} of {len(slowf)} taped; in-flight frames={sum(1 for f in dt if f > load)}')
print(' covered p50 us=', st.median(slowf[f] / 10 for f in fl), ' frame dt p50 ms=', st.median(dt[f][0] for f in fl))
names = sorted({n for f in fl for n in seg[f]}, key=lambda n: -st.median(seg[f].get(n, 0) for f in fl))
for n in names:
    v = [seg[f].get(n, 0) for f in fl]; print(f'  {n:18s} p50={st.median(v):8.0f} us  p95={sorted(v)[int(.95*len(v))]:8.0f}  present_in={sum(1 for f in fl if n in seg[f])}')
