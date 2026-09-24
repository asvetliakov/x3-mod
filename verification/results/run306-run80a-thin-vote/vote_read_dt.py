# dt on frames where the vote did buffer reads (cumulative reads grew) vs all frames
import re,glob,sys,statistics as st
log=glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log")[0]
prev=None;readf={};dt={};pe=None
for l in open(log,errors='replace'):
    if l.startswith('thin_vote_frame '):
        d=dict(re.findall(r'(\w+)=(\S+)',l))
        if prev and int(d['reads'])>int(prev['reads']): readf[int(d['frame'])]=(float(d['lock_us'])-float(prev['lock_us'])+float(d['measure_us'])-float(prev['measure_us']))
        prev=d
    elif l.startswith('frame_end '):
        d=dict(re.findall(r'(\w+)=(\S+)',l)); e=float(d['elapsed_ms']); f=int(d['frame'])
        if pe is not None: dt[f]=e-pe
        pe=e
r=[dt[f] for f in readf if f in dt]; a=[v for f,v in dt.items() if f>=300]
print('read frames',len(readf),'vote lock+measure us per read frame p50',st.median(readf.values()),'max',max(readf.values()))
print('dt on read frames p50',st.median(r),'max',max(r),' all frames>=300 p50',st.median(a))
print('read frames ranges',sorted(readf)[:5],'...',sorted(readf)[-5:])
