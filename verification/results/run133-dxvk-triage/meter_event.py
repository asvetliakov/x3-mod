"""In-flight meter_event_ready distribution and readback_lock_us by event state. Usage: meter_event.py <log>"""
import sys, re, statistics as st, collections
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)'); fe=[]; hf=[]
for raw in open(sys.argv[1],'rb'):
    if raw.startswith(b'frame_end '): d=dict(KV.findall(raw.decode('latin1'))); fe.append((int(d['frame']),float(d['dt_ms'])))
    elif raw.startswith(b'hdr_frame '):
        d=dict(KV.findall(raw.decode('latin1')))
        if 'readback_lock_us' in d: hf.append((int(d['frame']),d.get('meter_event_ready','absent'),float(d['readback_lock_us'])))
L=max(fe,key=lambda x:x[1])[0]; lo=L+10; hi=fe[-1][0]-10
s=[h for h in hf if lo<h[0]<hi]; c=collections.Counter(h[1] for h in s); print('inflight hdr rows',len(s),dict(c))
for k in c:
    x=sorted(h[2] for h in s if h[1]==k); print(f'event={k} n={len(x)} lock_p50={st.median(x):.1f} p95={x[int(len(x)*.95)]:.1f} p99={x[int(len(x)*.99)]:.1f} max={x[-1]:.1f} n>1ms={sum(v>1000 for v in x)}')
