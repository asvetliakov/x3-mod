"""Run 132 A: frame_phases window stats + frame_end hitch profile. Usage: python3 timing.py <session.log>"""
import sys, re, statistics as st
KV = re.compile(r'(\w+)=(\S+)')
ph = []; fe = []
with open(sys.argv[1], 'rb') as f:
    for raw in f:
        if raw.startswith(b'frame_phases '): ph.append(dict(KV.findall(raw.decode('latin1'))))
        elif raw.startswith(b'frame_end '): fe.append(dict(KV.findall(raw.decode('latin1'))))
print('frame_phases windows', len(ph))
for c in ['dt_p50_us','dt_p95_us','pre_render_p50_us','views_p50_us','present_p50_us','present_p95_us','view_submit_p50_us']:
    v = [int(d[c]) for d in ph if c in d]
    print(f'  {c}: median={st.median(v)} max={max(v)} min={min(v)}')
print('  windows dt_p95>20ms', sum(int(d['dt_p95_us'])>20000 for d in ph))
dt = [int(d.get('dt_ms',0)) for d in fe]; n = len(dt)
print('frame_end', n, 'median', st.median(dt), 'max', max(dt))
for th in (33, 50, 100):
    idx = [i for i,x in enumerate(dt) if x > th]
    q = [0,0,0,0]
    for i in idx: q[min(3, i*4//n)] += 1
    print(f'  >{th}ms: {len(idx)} quarters={q} first10={[(i,dt[i]) for i in idx[:10]]}')
big = sorted(range(n), key=lambda i: -dt[i])[:10]
print('  top10 (index,dt_ms):', [(i, dt[i]) for i in sorted(big)])
