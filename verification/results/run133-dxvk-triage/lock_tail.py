"""In-flight hdr_frame rows with readback_lock_us > 1 ms: frame, lock, frame_end dt_ms, runs of consecutive frames. Usage: lock_tail.py <log>"""
import sys, re
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)'); fe={}; hf=[]
for raw in open(sys.argv[1],'rb'):
    if raw.startswith(b'frame_end '): d=dict(KV.findall(raw.decode('latin1'))); fe[int(d['frame'])]=float(d['dt_ms'])
    elif raw.startswith(b'hdr_frame '):
        d=dict(KV.findall(raw.decode('latin1')))
        if float(d.get('readback_lock_us',0))>1000: hf.append((int(d['frame']),float(d['readback_lock_us']),d.get('meter_event_ready')))
L=max(fe,key=fe.get); lo=L+10; hi=max(fe)-10; s=[h for h in hf if lo<h[0]<hi]
print('load',L,'n',len(s),'lock_sum_ms',round(sum(h[1] for h in s)/1000,1))
runs=[];
for h in s:
    if runs and h[0]-runs[-1][-1][0]<=2: runs[-1].append(h)
    else: runs.append([h])
for r in runs: print(f'frames {r[0][0]}-{r[-1][0]} n={len(r)} lock_ms max={max(h[1] for h in r)/1000:.1f} dt_ms={[fe.get(h[0]) for h in r][:8]}')
