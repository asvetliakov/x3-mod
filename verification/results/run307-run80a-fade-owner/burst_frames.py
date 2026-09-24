# per burst frame: fade_routed, camera_rotation_deg, camera_cut, taa_invalidate rows nearby; pan windows around burst
import glob,re,sys
r=sys.argv[1]; L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
B=sorted(int(p.split('_')[-1].split('.')[0]) for p in glob.glob(f"/tmp/x3-bottleX3-run{r}/depth_1_*.rgba32f"))
S=set(); 
for b in B: S.update(range(b-30,b+31))
fr={};rot={};inv=[]
for l in open(L,errors='replace'):
    if l.startswith('fade_route_frame'):
        f=int(re.search(r' frame=(\d+)',l).group(1))
        if f in S: fr[f]=int(re.search(r'fade_routed=(\d+)',l).group(1))
    elif l.startswith('motion_output_frame'):
        f=int(re.search(r' frame=(\d+)',l).group(1))
        if f in S:
            m=re.search(r' camera_rotation_deg=(\S+)',l); rot[f]=float(m.group(1)) if m else None
    elif l.startswith('taa_invalidate'):
        inv.append(l.strip()[:160])
for b in B: print(b,'fade_routed',fr.get(b),'rot_deg',rot.get(b))
import statistics as st
for s in sorted({b for b in B if b-1 not in B}):
    w=[rot[f] for f in range(s-30,s) if rot.get(f) is not None]
    print('burst',s,'pre30 rot median',st.median(w),'max',max(w),'fade_routed pre30 median',st.median([fr.get(f,0) for f in range(s-30,s)]))
print('taa_invalidate rows',len(inv)); import collections
print(collections.Counter(re.sub(r'=\S+','=',x) for x in inv).most_common(5)); print(inv[:3])
