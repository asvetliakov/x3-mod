# per burst frame: camera translation, step length vs previous frame, rotation_deg, jitter, taa weight/k, draws/routed, thin_vote voted
# usage: camera_rows.py RUN F0 N
import sys,glob,re,math
r=sys.argv[1]; F0,N=int(sys.argv[2]),int(sys.argv[3]); L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
W=set(range(F0-1,F0+N)); cam={};mo={};tv={}
def kv(l): return dict(x.split('=',1) for x in l.split()[1:] if '=' in x)
for l in open(L,'rb'):
    if l.startswith((b'camera_state ',b'motion_output_frame ',b'thin_vote_frame ')):
        m=re.search(rb' frame=(\d+) ',l)
        if not m or int(m.group(1)) not in W: continue
        d=kv(l.decode(errors='replace')); f=int(d['frame'])
        {b'c':cam,b'm':mo,b't':tv}[l[:1]][f]=d
for f in range(F0,F0+N):
    c=cam.get(f,{}); p=cam.get(f-1,{}); m=mo.get(f,{}); t=tv.get(f,{})
    step='-'
    if 't' in c and 't' in p:
        a=list(map(float,c['t'].split(','))); b=list(map(float,p['t'].split(','))); step=f"{math.dist(a,b):.2f}"
    print(f,'t',c.get('t'),'step',step,'rot_deg',c.get('rotation_deg'),'cut',c.get('camera_cut'),'jit',m.get('jitter_x'),m.get('jitter_y'),
          'draws',m.get('draws'),'routed',m.get('routed'),'taa_weight',m.get('taa_weight'),'taa_k',m.get('taa_k'),'hist',m.get('taa_history'),
          'voted',t.get('voted'),'opaque',t.get('opaque'),'known',t.get('known'),'min_alpha',t.get('min_alpha'))
