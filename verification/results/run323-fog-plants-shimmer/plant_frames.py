# Per session: frames with fade_tested > 0 (alpha-tested fade-band cutouts = the fog-band plants' panels/trusses routed),
# grouped into contiguous spans, with camera_rotation_deg / cut_median_px over each span (motion_output_frame), and the DLL identity row.
# usage: plant_frames.py RUN
import sys,glob,re
L=glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log")[0]
T={};M={};ident=None
for l in open(L,'rb'):
    if l.startswith(b'fade_route_frame '):
        d=dict(kv.split('=',1) for kv in l.decode(errors='replace').split()[1:] if '=' in kv); T[int(d['frame'])]=int(d.get('fade_tested',0))
    elif l.startswith(b'motion_output_frame '):
        d=dict(kv.split('=',1) for kv in l.decode(errors='replace').split()[1:] if '=' in kv)
        M[int(d['frame'])]=(float(d.get('camera_rotation_deg',0)),float(d.get('cut_median_px',0)))
    elif ident is None and (l.startswith(b'proxy_build ') or l.startswith(b'proxy_module ') or l.startswith(b'x3m_build ')): ident=l[:200].decode(errors='replace').strip()
print('identity',ident)
fr=sorted(f for f,v in T.items() if v>0); spans=[]
for f in fr:
    if spans and f-spans[-1][1]<=30: spans[-1][1]=f
    else: spans.append([f,f])
for a,b in spans:
    m=[M[f] for f in range(a,b+1) if f in M]; rot=[x[0] for x in m]; cut=[x[1] for x in m]
    mv=sum(1 for x in rot if x>0.05)
    print(f"span {a}-{b} frames {b-a+1} fade_tested max {max(T.get(f,0) for f in range(a,b+1))} rot>0.05deg frames {mv} rot p50/max {sorted(rot)[len(rot)//2]:.3f}/{max(rot):.3f} cut p50/max {sorted(cut)[len(cut)//2]:.2f}/{max(cut):.2f}")
