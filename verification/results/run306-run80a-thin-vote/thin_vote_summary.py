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
# Rows from builds with the split (deferred_cap/already_queued, draw_timing/sampled/sample_us/stamp_us) print them in the
# table above; older rows derive it: missed = queued + dropped + already_queued (a subset past read_attempts is Unreadable
# and counts under unreadable, not missed).
M=lambda k: sum(float(r.get(k,0)) for r in rows)
if 'deferred_cap' not in rows[0]:
    rest=M('missed')-M('queued')-M('dropped')
    print(f"missed split (derived): queued={M('queued'):.0f} deferred_cap(dropped)={M('dropped'):.0f} already_queued={rest:.0f}")
else:
    print(f"missed split: queued={M('queued'):.0f} deferred_cap={M('deferred_cap'):.0f} already_queued={M('already_queued'):.0f} missed={M('missed'):.0f}")
    print('draw_timing values', sorted({r['draw_timing'] for r in rows}))
    samp=[r for r in rows if r.get('sampled')=='1']
    if samp:
        su=sorted(float(r['sample_us']) for r in samp); st=sorted(float(r['stamp_us']) for r in samp)
        est=sorted(float(r['sample_us'])*float(r['opaque']) for r in samp)
        print(f"sampled frames={len(samp)} sample_us p50={su[len(su)//2]:.2f} p99={su[int(len(su)*.99)]:.2f} stamp_us p50={st[len(st)//2]:.2f}"
              f" est_frame_us(sample_us*opaque) p50={est[len(est)//2]:.1f} p99={est[int(len(est)*.99)]:.1f}")
