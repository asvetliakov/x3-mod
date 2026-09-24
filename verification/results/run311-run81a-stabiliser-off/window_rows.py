# per window of frames: fade_route_frame (routed, overlay_refused, evicted, owner_masked), motion_output_frame
# (unjittered_depth_writers, camera_rotation_deg, taa_resolved), thin_vote_frame (draws, voted, min_alpha)
# usage: window_rows.py RUN LO-HI ...
import sys,glob,re,statistics as st
r=sys.argv[1]; W=[tuple(map(int,w.split('-'))) for w in sys.argv[2:]]
L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
D={}
pat=re.compile(rb'^(fade_route_frame|motion_output_frame|thin_vote_frame) device=1 frame=(\d+) ')
keys={b'fade_route_frame':('fade_routed','overlay_refused','fade_evicted','fade_owner_masked','fade_refused'),
      b'motion_output_frame':('unjittered_depth_writers','camera_rotation_deg','taa_resolved','routed'),
      b'thin_vote_frame':('draws','voted','min_alpha','refused')}
for l in open(L,'rb'):
    m=pat.match(l)
    if not m: continue
    f=int(m.group(2))
    if not any(lo<=f<=hi for lo,hi in W): continue
    d=D.setdefault(f,{})
    for k in keys[m.group(1)]:
        mm=re.search(rb' '+k.encode()+rb'=([-\d.]+)',l)
        if mm: d[m.group(1).decode()[:4]+'.'+k]=float(mm.group(1))
for lo,hi in W:
    fs=[f for f in range(lo,hi+1) if f in D]; print(f"window {lo}-{hi} frames {len(fs)}")
    for k in sorted({k for f in fs for k in D[f]}):
        v=[D[f][k] for f in fs if k in D[f]]
        print(f"  {k}: min {min(v):g} median {st.median(v):g} max {max(v):g} nonzero {sum(1 for x in v if x)}")
