# per-frame fade_route_frame and motion_output_frame fields (fade owner, routed, refused, unjittered writers, taa_invalidate)
import sys,glob,re,collections
for r in sys.argv[1:]:
    L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
    F=collections.defaultdict(list); keys=set(); unj=0; mof=0; ov=0
    for line in open(L,'rb'):
        if line.startswith(b'fade_route_frame '):
            d=dict(kv.split('=',1) for kv in line.decode().split()[1:] if '=' in kv)
            for k,v in d.items():
                if k not in('device','frame'): F[k].append(v)
        elif line.startswith(b'motion_output_frame '):
            m=re.search(rb' unjittered_depth_writers=(\d+)',line); mof+=1
            if m and int(m.group(1)): unj+=1
        elif b'overlay_node' in line: ov+=1
    n=len(F['fade_routed']) if F else 0
    print(f"run{r} fade_route_frame rows {n}")
    for k,v in F.items():
        try: x=[float(t) for t in v]
        except: print(' ',k,collections.Counter(v).most_common(3)); continue
        nz=sum(1 for t in x if t)
        print(f"  {k}: nonzero_frames {nz} ({nz/max(n,1):.3f}) sum {sum(x):.0f} max {max(x):.0f} distinct {sorted(set(x))[:6]}")
    print(f"  motion_output_frame {mof} frames with unjittered_depth_writers>0: {unj}; lines containing overlay_node: {ov}")
