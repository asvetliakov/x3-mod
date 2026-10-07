"""Run 9 (wined3d) vs run 10 (DXVK) A/B: in-flight phase columns and frame time by draw count.
In flight = frames after the load frame (largest dt_ms) +10 and before the last 10. Usage: ab.py <log>"""
import sys, re, statistics as st
KV = re.compile(r'(\w+)=(\S+)'); ph=[]; fe=[]
for raw in open(sys.argv[1],'rb'):
    if raw.startswith(b'frame_phases '): ph.append({k:int(v) for k,v in KV.findall(raw.decode('latin1')) if v.isdigit()})
    elif raw.startswith(b'frame_end '): fe.append({k:int(v) for k,v in KV.findall(raw.decode('latin1')) if v.isdigit()})
L=max(range(len(fe)),key=lambda i:fe[i]['dt_ms']); lo=fe[L]['frame']+10; hi=fe[-1]['frame']-10
s=[d for d in fe if lo<d['frame']<hi]; w=[d for d in ph if d['frame']-d['frames']>=lo and d['frame']<=hi]
print('load_frame',fe[L]['frame'],'inflight_frames',len(s),'windows',len(w))
for c in sorted({k for d in w for k in d if k.endswith('_us') or k=='views_p50'}):
    v=[d[c] for d in w if c in d]; print(f'PH {c} median={st.median(v)} max={max(v)}')
dt=[d['dt_ms'] for d in s]; h=len(dt)//2
for th in (50,100): print(f'>{th}ms total={sum(x>th for x in dt)} first={sum(x>th for x in dt[:h])} second={sum(x>th for x in dt[h:])}')
for key in ('draws','issued'):
    v=sorted(d[key] for d in s); q=[v[len(v)*i//4] for i in (1,2,3)]
    print(f'{key} quartile edges {q} min={v[0]} max={v[-1]}')
    for b,(a,z) in enumerate(zip([-1]+q,q+[10**9])):
        x=[d['dt_ms'] for d in s if a<d[key]<=z]
        if x: print(f'  {key} bin{b} ({a},{z}] n={len(x)} dt_p50={st.median(x)} dt_p95={sorted(x)[int(len(x)*.95)]} mean={st.mean(x):.2f}')
