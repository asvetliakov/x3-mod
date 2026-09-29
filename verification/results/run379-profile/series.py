# Per-report ms/frame of the main slot-0 frame buckets vs frame_phases pre_render/views (run379).
import re,glob,collections,bisect
L=glob.glob('/tmp/x3-bottleX3-run379/session-*.log')[0]
kv=lambda s:dict(re.findall(r'(\w+)=(\S+)',s))
reps=[];cur=None;fr=[];ph=[]
for line in open(L,errors='replace'):
    if line.startswith('frame_end '):
        d=kv(line);fr.append((int(d['qpc']),int(d['frame'])))
    elif line.startswith('frame_phases '):
        d=kv(line);ph.append((int(d['frame']),int(d['pre_render_p50_us'])/1e3,int(d['views_p50_us'])/1e3))
    elif line.startswith('profile_report scope=delta'):
        cur={'h':kv(line),'F':collections.Counter(),'s0':0};reps.append(cur)
    elif line.startswith('profile_report scope=cumulative'): cur=None
    elif cur is None: continue
    elif line.startswith('profile_frame ') and ' slot=0 ' in line:
        d=kv(line);cur['F'][0x400000+int(d['rva'],16)]+=int(d['count'])
    elif line.startswith('profile_thread ') and ' slot=0 ' in line:
        cur['s0']=int(kv(line)['samples'])
q=[f[0] for f in fr]
B=[0x400000,0x4d6509,0x4c403e,0x48f2dd,0x4721b6,0x4e25a8,0x50e21e,0x4d358f]
print('rep frames      ms/fr  pre  views |'+' '.join(f'{b:08x}' for b in B)+'  other  (ms/frame per bucket)')
for i,r in enumerate(reps):
    e=int(r['h']['qpc']);s=e-int(float(r['h']['elapsed_us'])*10)
    a=bisect.bisect_left(q,s);b=bisect.bisect_right(q,e)-1;n=b-a+1
    if n<20 or r['s0']==0: continue
    msf=float(r['h']['elapsed_us'])/1e3/n
    f0,f1=fr[a][1],fr[b][1];mid=(f0+f1)/2
    pp=[p for p in ph if f0<=p[0]<=f1+150] or [min(ph,key=lambda p:abs(p[0]-mid))]
    pre=sum(p[1] for p in pp)/len(pp);vw=sum(p[2] for p in pp)/len(pp)
    vals=[r['F'][k]/r['s0']*msf for k in B];oth=msf-sum(vals)
    print(f"R{i:02d} {f0:4d}-{f1:4d} {msf:5.1f} {pre:5.1f} {vw:5.1f} |"+' '.join(f'{v:8.2f}' for v in vals)+f' {oth:6.2f}')
