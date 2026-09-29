# engine_memory summary deltas per 5 s, aligned to the last frame_end before each row (run379).
import re,glob
L=glob.glob('/tmp/x3-bottleX3-run379/session-*.log')[0]
kv=lambda s:dict(re.findall(r'(\w+)=(\S+)',s))
last=0;prev=None;rows=[]
for line in open(L,errors='replace'):
    if line.startswith('frame_end '): last=int(kv(line)['frame'])
    elif line.startswith('engine_memory phase=summary'):
        d=kv(line); rows.append((last,int(d['reads']),int(d['queries']),d['path']))
out=[]
for (f0,r0,q0,_),(f1,r1,q1,p) in zip(rows,rows[1:]):
    if f1>f0: out.append((f0,f1,(r1-r0)/(f1-f0),(q1-q0)/(f1-f0),p))
for o in out[::4]: print("frames %5d-%5d reads/frame %8.0f queries/frame %6.1f path=%s"%o)
