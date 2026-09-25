"""Run336: caster-set flip frames vs camera rotation/translation per frame.
Usage: flips_vs_camera.py session.log rows.json"""
import sys,json,math,collections
cam={}
for line in open(sys.argv[1],errors='replace'):
    if not line.startswith('camera_state '): continue
    d=dict(t.split('=',1) for t in line.split()[1:] if '=' in t)
    try:
        fr=int(d['frame']); r=[float(d[f'r{i}{j}']) for i in range(3) for j in range(3)]
        t=[float(x) for x in d['t'].split(',')]
    except Exception: continue
    cam[fr]=(r,t)
R=json.load(open(sys.argv[2])); can=R['shadow_replay_candidates']
def ang(a,b):
    # angle between forward rows (third row) of consecutive rotations, degrees
    fa=a[6:9]; fb=b[6:9]
    c=sum(x*y for x,y in zip(fa,fb)); c=max(-1,min(1,c)); return math.degrees(math.acos(c))
rot={};mov={}
for fr in cam:
    if fr-1 in cam:
        rot[fr]=ang(cam[fr-1][0],cam[fr][0]); mov[fr]=math.dist(cam[fr-1][1],cam[fr][1])
def pct(v,p): v=sorted(v); return v[min(len(v)-1,int(p/100*len(v)))]
allrot=list(rot.values()); print('camera frames',len(cam),'rot deg/frame p50=%.3f p90=%.3f p99=%.3f'%(pct(allrot,50),pct(allrot,90),pct(allrot,99)))
allm=list(mov.values()); print('move u/frame p50=%.1f p99=%.1f max=%.0f'%(pct(allm,50),pct(allm,99),max(allm)))
big=[fr for fr,m in mov.items() if m>5000]; print('translation jumps >5000 u/frame', len(big), big[:10])
for i in range(5):
    fl=[(int(d['frame']),float(d.get(f'flip_c{i}',0))) for d in can if float(d.get(f'flip_c{i}',0))>=5]
    r=[rot.get(fr,float('nan')) for fr,_ in fl]; m=[mov.get(fr,float('nan')) for fr,_ in fl]
    print(f'c{i} flip>=5 frames={len(fl)} sample={fl[:8]} rot_deg median={pct(r,50) if r else 0:.3f} move median={pct(m,50) if m else 0:.1f}')
# large flips in context of toggles / first frames
