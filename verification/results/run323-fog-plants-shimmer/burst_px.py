# per burst frame: fade_route_frame fields (routed/tested/owner_masked/overlay_refused) + motion_output_frame cut_median_px, camera_rotation_deg, jitter, unjittered writers
import sys,glob,re
r=sys.argv[1]; L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
B=set(sorted(int(p.split('_')[-1].split('.')[0]) for p in glob.glob(f"/tmp/x3-bottleX3-run{r}/depth_1_*.rgba32f")))
R={}
for l in open(L,'rb'):
    for pre,keys in ((b'fade_route_frame ',('fade_routed','fade_tested','fade_owner_masked','overlay_refused','fade_evicted')),(b'motion_output_frame ',('cut_median_px','cut_samples','camera_rotation_deg','camera_cut','unjittered_depth_writers','routed','draws'))):
        if l.startswith(pre):
            d=dict(kv.split('=',1) for kv in l.decode().split()[1:] if '=' in kv); f=int(d['frame'])
            if f in B: R.setdefault(f,{}).update({k:d.get(k) for k in keys})
for f in sorted(R): print(f,' '.join(f"{k}={v}" for k,v in R[f].items()))
