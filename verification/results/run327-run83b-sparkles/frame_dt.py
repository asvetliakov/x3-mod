# frame_timing dt_p50_us for the 300-frame window containing each given frame (fps context for the burst)
# usage: frame_dt.py RUN FRAME...
import sys,glob
L=glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log")[0]; want=[int(x) for x in sys.argv[2:]]; rows=[]
for l in open(L,'rb'):
    if l.startswith(b'frame_timing '):
        d=dict(kv.split('=',1) for kv in l.decode(errors='replace').split()[1:] if '=' in kv); rows.append((int(d['frame']),int(d['frames']),d['dt_p50_us'],d['dt_p95_us']))
for w in want:
    for f,n,p50,p95 in rows:
        if f-n<w<=f: print(f"run{sys.argv[1]} frame {w} window {f-n+1}-{f} dt_p50_us {p50} dt_p95_us {p95}")
