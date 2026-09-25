"""Run336: per-frame caster churn (flip_c<i>, leased delta) before/after the frame-906 flush onset and around camera turns.
Usage: churn_after_flush.py session.log rows.json"""
import sys,json,math
cam={}
for line in open(sys.argv[1],errors='replace'):
    if line.startswith('camera_state '):
        d=dict(t.split('=',1) for t in line.split()[1:] if '=' in t)
        try: cam[int(d['frame'])]=[float(d[f'r2{j}']) for j in range(3)]
        except Exception: pass
rot={f:math.degrees(math.acos(max(-1,min(1,sum(a*b for a,b in zip(cam[f-1],cam[f])))))) for f in cam if f-1 in cam}
R=json.load(open(sys.argv[2])); can={int(d['frame']):d for d in R['shadow_replay_candidates']}
def seg(lo,hi,label):
    fr=[f for f in can if lo<=f<=hi]
    tot=[sum(float(can[f].get(f'flip_c{i}',0)) for f in fr) for i in range(5)]
    print(f'{label} frames={len(fr)} flip sums c0..c4={tot} per1000={[round(1000*t/max(1,len(fr)),1) for t in tot]}')
seg(404,905,'retention live 404-905'); seg(906,19896,'flushed 906-19896')
turns=[f for f,a in rot.items() if a>=0.5 and f>=906]
print('frames>=906 with forward rotation >=0.5 deg/frame', len(turns), ' >=10 deg', sum(1 for f in turns if rot[f]>=10))
still=[f for f,a in rot.items() if a<0.05 and f>=906]
for name,fs in (('turning',turns),('still',still)):
    fs=[f for f in fs if f in can]
    s=[sum(float(can[f].get(f'flip_c{i}',0)) for i in range(5)) for f in fs]
    print(f'  {name} frames={len(fs)} flips/frame mean={sum(s)/max(1,len(s)):.3f} frames_with_flip={sum(1 for x in s if x>0)}')
# leased change on turns
d=[abs(float(can[f]['leased'])-float(can[f-1]['leased'])) for f in turns if f in can and f-1 in can]
print('  |delta leased| on turning frames mean=%.2f max=%.0f'%(sum(d)/max(1,len(d)),max(d or [0])))
