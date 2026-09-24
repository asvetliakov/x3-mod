import sys,re,glob
log=glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log")[0]
keys=['reads','measured','triangles','lock_us','measure_us','max_measure_us','invalidated','dropped_entries','unreadable_total']
prev=None;dec={k:0 for k in keys};inc={k:[] for k in keys}
for l in open(log,errors='replace'):
    if not l.startswith('thin_vote_frame '): continue
    d=dict(re.findall(r'(\w+)=(\S+)',l)); f=int(d['frame'])
    if prev:
        for k in keys:
            a,b=float(prev[k]),float(d[k])
            if b<a: dec[k]+=1
            elif b>a: inc[k].append((f,b-a))
    prev=d
for k in keys:
    s=sorted(x[1] for x in inc[k])
    print(k,'decreases',dec[k],'increase_frames',len(s),'total_increase',round(sum(s),1),'max_step',s[-1] if s else 0, 'last_frame_inc', inc[k][-1][0] if inc[k] else None)
print('final',{k:prev[k] for k in keys})
# steps of lock_us per frame: list the frames with biggest increment
print('top lock_us steps',sorted(inc['lock_us'],key=lambda x:-x[1])[:10])
print('top measure_us steps',sorted(inc['measure_us'],key=lambda x:-x[1])[:10])
