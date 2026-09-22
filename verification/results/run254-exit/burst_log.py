"""Run 254 per-frame burst table from extract_log.sh output: translation |t_f - t_{f-1}|, view-rotation
change angle between successive camera_state r matrices (deg, own computation), the logged rotation_deg,
gate6, cut. Usage: burst_log.py lines.txt"""
import sys,re,math,numpy as np
kv=lambda l:dict(re.findall(r'(\w+)=(\S+)',l))
cam={};mof={};ums=[]
for l in open(sys.argv[1]):
    k=l.split(' ',1)[0];d=kv(l)
    if k=='camera_state' and 'frame' in d:cam[int(d['frame'])]=d
    elif k=='motion_output_frame':mof[int(d['frame'])]=d
    elif k=='motion_unmatched_static_frame':ums.append((int(d['frame']),d.get('applied')))
    elif k=='capture_armed':print('armed',d['frame'],'->',d['start_frame'])
R=lambda c:np.array([[float(c[f'r{i}{j}']) for j in range(3)] for i in range(3)])
for s in (5496,6875,11177):
    print(f'burst {s}-{s+31}: frame trans dang_deg rotation_deg gate6 cut')
    for f in range(s,s+32):
        c=cam.get(f);p=cam.get(f-1);m=mof.get(f,{})
        tr=da=float('nan')
        if c and p:
            tr=math.dist([float(x) for x in c['t'].split(',')],[float(x) for x in p['t'].split(',')])
            M=R(c)@R(p).T;da=math.degrees(math.acos(max(-1,min(1,(np.trace(M)-1)/2))))
        print(f'  {f} {tr:.1f} {da:.4f} {c["rotation_deg"] if c else "-"} {m.get("gate6","-")} {m.get("camera_cut","-")}')
    print('  ums',[u for u in ums if s<=u[0]<s+32])
