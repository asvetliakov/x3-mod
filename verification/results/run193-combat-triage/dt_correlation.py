import re,sys,collections,statistics as st
F=collections.defaultdict(dict)
num=re.compile(r'^-?\d+(\.\d+)?$')
for l in open(sys.argv[1],errors='replace'):
    m=re.search(r' frame=(\d+) ',l)
    if not m or not l.startswith(tuple('abcdefghijklmnopqrstuvwxyz')): continue
    k=l.split(' ',1)[0]; f=int(m.group(1))
    for a,b in re.findall(r'(\w+)=(\S+)',l):
        if num.match(b): F[f][k+'.'+a]=F[f].get(k+'.'+a,0)+float(b)
fr=[f for f in F if 1490<=f<=4470 and 'frame_end.dt_ms' in F[f]]
dt=[F[f]['frame_end.dt_ms'] for f in fr]
keys=set(); [keys.update(F[f]) for f in fr]
res=[]
for k in keys:
    v=[F[f].get(k,0) for f in fr]
    if len(set(v))<3: continue
    try: c=st.correlation(v,dt)
    except: continue
    res.append((c,k,st.mean(v)))
for c,k,m in sorted(res,reverse=True)[:30]: print(f'{c:.3f} {k} mean={m:.1f}')
print('--- _us fields')
for c,k,m in sorted(res,reverse=True):
    if k.endswith('_us') or k.endswith('_ms'): print(f'{c:.3f} {k} mean={m:.1f}')
