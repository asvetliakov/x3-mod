# Map run379 profiler delta reports to frame ranges and frame_phases rows.
import re,glob,bisect
L=glob.glob('/tmp/x3-bottleX3-run379/session-*.log')[0]
fr=[];rep=[];ph=[]
kv=lambda s:dict(re.findall(r'(\w+)=(\S+)',s))
for line in open(L,errors='replace'):
    if line.startswith('frame_end '):
        d=kv(line);fr.append((int(d['qpc']),int(d['frame']),int(d.get('draws',0))))
    elif line.startswith('profile_report scope=delta'):
        d=kv(line);rep.append(d)
    elif line.startswith('frame_phases '):
        d=kv(line);ph.append(d)
q=[f[0] for f in fr]
for i,d in enumerate(rep):
    e=int(d['qpc']);s=e-int(float(d['elapsed_us'])*10)
    a=bisect.bisect_left(q,s);b=bisect.bisect_right(q,e)-1
    n=b-a+1; dr=sorted(f[2] for f in fr[a:b+1])
    print(f"R{i:02d} t={float(d['since_start_us'])/1e6:6.1f}s frames {fr[a][1]}-{fr[b][1]} n={n} ms/frame={5000/max(n,1):.1f} draws_p50={dr[len(dr)//2] if dr else 0} samples={d['samples']} ticks={d['ticks']} threads={d['threads']} tick_us_mean={d['tick_us_mean']}")
for d in ph:
    print('phases frame',d['frame'],'dt',d['dt_p50_us'],'pre',d['pre_render_p50_us'],'views',d['views_p50_us'])
