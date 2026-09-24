import sys,re,glob
log=glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log")[0]
rows=[]
for l in open(log,errors='replace'):
    if l.startswith('thin_vote_frame '):
        rows.append({k:v for k,v in re.findall(r'(\w+)=(\S+)',l)})
print('rows',len(rows),'keys',' '.join(rows[0].keys()))
num=lambda v: float(v) if re.fullmatch(r'-?[\d.]+',v) else None
for k in rows[0]:
    vals=[num(r.get(k,'x')) for r in rows]; vals=[v for v in vals if v is not None]
    if not vals or k in('device','frame'): continue
    nz=sum(1 for v in vals if v); s=sorted(vals)
    print(f"{k:18} nonzero_frames={nz:5} sum={sum(vals):.1f} max={s[-1]:.2f} p50={s[len(s)//2]:.2f} p99={s[int(len(s)*.99)]:.2f} min={s[0]:.4f}")
act=[r for r in rows if float(r['voted'])>0]
if act: print('first voted frame',act[0]['frame'],'last',act[-1]['frame'],'n',len(act))
top=sorted(rows,key=lambda r:-float(r.get('lock_us','0')))[:8]
print('top lock_us', [(r['frame'],r['lock_us'],r.get('reads'),r.get('triangles')) for r in top])
