"""Run 249 burst table from a pre-filtered log (grep of motion_output_frame/camera_state/
motion_unmatched_static_frame/capture_armed lines for the burst frames). Usage: burst_log.py lines.txt"""
import sys,re,math
kv=lambda l:dict(re.findall(r'(\w+)=(\S+)',l))
cam={};mof={};ums=[]
for l in open(sys.argv[1]):
    k=l.split(' ',1)[0];d=kv(l)
    if k=='camera_state':cam[int(d['frame'])]=d
    elif k=='motion_output_frame':mof[int(d['frame'])]=d
    elif k=='motion_unmatched_static_frame':ums.append((int(d['frame']),d.get('applied')))
    elif k=='capture_armed':print('armed',d['frame'],'->',d['start_frame'])
for s in (3515,4150,5538,9267):
    tr=[];rot=[];g6=0;cut=0;r00=[]
    for f in range(s,s+32):
        c=cam.get(f);p=cam.get(f-1)
        if c and p:
            a=[float(x) for x in c['t'].split(',')];b=[float(x) for x in p['t'].split(',')]
            tr.append(math.dist(a,b))
        if c: rot.append(float(c['rotation_deg']));r00.append(float(c['r00']));cut+=int(c['camera_cut'])
        m=mof.get(f)
        if m: g6+=int(m['gate6'])
    print(f'burst {s}-{s+31}: trans/frame min {min(tr):.1f} med {sorted(tr)[len(tr)//2]:.1f} max {max(tr):.1f}; rotation_deg {min(rot):.3f}..{max(rot):.3f}; r00 {r00[0]:.3f}->{r00[-1]:.3f}; cuts {cut}; gate6 sum {g6}; ums {[u for u in ums if s<=u[0]<s+32]}')
