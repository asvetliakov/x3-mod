# thin_vote_frame aggregates per run: frames, nonzero counts/sums/max for selected fields, sample_us/measure_us percentiles
import sys,glob,numpy as np
K=['voted','missed','unreadable','dropped','draw_timing','sample_us','measure_us','max_measure_us','lock_us','invalidated','dropped_entries','overflows','refused','volatile_refused','lock_failed','deferred_cap','stale']
for r in sys.argv[1:]:
    L=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]; V={k:[] for k in K}; n=0
    for line in open(L,'rb'):
        if not line.startswith(b'thin_vote_frame '): continue
        d=dict(kv.split('=',1) for kv in line.decode().split()[1:] if '=' in kv); n+=1
        for k in K: V[k].append(float(d.get(k,0)))
    print(f"run{r} thin_vote_frame rows {n}")
    for k in K:
        x=np.array(V[k]); nz=int((x!=0).sum())
        print(f"  {k}: nonzero {nz} sum {x.sum():.1f} p50 {np.median(x):.2f} p99 {np.percentile(x,99):.2f} max {x.max():.2f}")
