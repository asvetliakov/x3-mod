"""Run336: frames >=906 with camera forward rotation >=10 deg: flips and live caster count, before/after.
Usage: turns.py session.log rows.json"""
import sys,json,math,os
THR=float(os.environ.get("THR","10"))
cam={}
for line in open(sys.argv[1],errors='replace'):
    if line.startswith('camera_state '):
        d=dict(t.split('=',1) for t in line.split()[1:] if '=' in t)
        try: cam[int(d['frame'])]=[float(d[f'r2{j}']) for j in range(3)]
        except Exception: pass
R=json.load(open(sys.argv[2])); can={int(d['frame']):d for d in R['shadow_replay_candidates']}; ret={int(d['frame']):d for d in R['shadow_retention_frame']}
for f in sorted(cam):
    if f<906 or f-1 not in cam or f not in can: continue
    a=math.degrees(math.acos(max(-1,min(1,sum(x*y for x,y in zip(cam[f-1],cam[f]))))))
    if a<THR: continue
    print(f, round(a,1), 'leased', can[f-1]['leased'],'->',can[f]['leased'], 'nodes', ret[f-1]['nodes_live'],'->',ret[f]['nodes_live'], 'flips', sum(int(can[f][f'flip_c{i}']) for i in range(5)))
