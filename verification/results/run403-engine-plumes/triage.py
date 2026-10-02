#!/usr/bin/env python3
"""Run403 engine plumes triage: streams the session log, prints counts only."""
import glob, statistics as st, sys, collections as C
def kv(line):
    d={}
    for t in line.split()[1:]:
        if '=' in t:
            k,v=t.split('=',1); d[k]=v
    return d
def log(run): return glob.glob(f'/tmp/x3-bottleX3-{run}/session-*.log')[0]
def pct(a,p):
    a=sorted(a); return a[min(len(a)-1,int(p*len(a)))] if a else None
def dts(run,lo,hi):
    r=[]
    for l in open(log(run),errors='replace'):
        if l.startswith('frame_end '):
            d=kv(l); f=int(d['frame'])
            if lo<=f<=hi and int(d.get('capture','0'))==0: r.append(float(d['dt_ms']))
    return r
L=log('run403')
st_rows=[];ef={};caps=C.Counter();other=C.Counter();fail=0;frames_end={}
for l in open(L,errors='replace'):
    h=l.split(' ',1)[0]
    if h=='engine_stage': st_rows.append(kv(l))
    elif h=='engine_frame': d=kv(l); ef[int(d['frame'])]=d
    elif h=='capture_event': caps[int(kv(l).get('frame',-1))]+=1
    elif h=='engine_plumes_failed': fail+=1
    elif h=='frame_end':
        d=kv(l); frames_end[int(d['frame'])]=d
    if ('error' in l.lower() or 'refus' in l.lower() or 'fail' in l.lower()) and h not in('engine_stage','engine_frame','frame_end'): other[h]+=1
arm=[r for r in st_rows if r['armed']=='1']
print('stage rows',len(st_rows),'armed',len(arm),'first armed',arm[0]['frame'] if arm else None,
      'armed->0 after arm', sum(1 for r in st_rows if arm and int(r['frame'])>int(arm[0]['frame']) and r['armed']=='0'))
print('reasons',C.Counter(r['reason'] for r in st_rows))
print('engine_plumes_failed',fail,'nonzero step',sum(r['step']!='0' for r in arm),'ribbon_step!=0',sum(r['ribbon_step']!='0' for r in arm),
      'result',C.Counter(r['result'] for r in arm),'ribbon_result',C.Counter(r['ribbon_result'] for r in arm))
def s(k,rows=arm,f=float):
    a=[f(r[k]) for r in rows]; return (min(a),st.median(a),pct(a,.9),max(a),sum(a)) if a else None
for k in ['nozzles','records','ribbons','ribbons_live','ribbons_fading','ribbon_samples','ribbon_created','ribbon_appended','ribbon_evicted','ribbon_cut_clear','ribbon_load_clear','ribbon_overflow','skipped_other_view','ribbon_skipped_other_view','capped','faded','culled_small','culled_behind','culled_idle','fogged','stage_us']:
    print(k,'min/med/p90/max/sum',s(k))
print('nozzles>0 frames',sum(float(r['nozzles'])>0 for r in arm),'ribbons>0 frames',sum(float(r['ribbons'])>0 for r in arm))
last=st_rows[-1]
print('totals', {k:last[k] for k in last if k.endswith('_total')})
print('view_rule',C.Counter(r['view_rule'] for r in arm))
print('fog',C.Counter(r['fog'] for r in arm),'fog_min range',s('fog_min'))
fr=[r for r in arm if float(r['fogged'])>0]; print('frames fogged>0',len(fr),'fog_min on those',s('fog_min',fr) if fr else None)
print('preset dist',C.Counter(r['preset'] for r in arm))
print('stage_us nozzles>0 med/p90/p99/max',(lambda a:(st.median(a),pct(a,.9),pct(a,.99),max(a)))([float(r['stage_us']) for r in arm if float(r['nozzles'])>0]))
capf=sorted(caps); print('capture frames',capf, 'events/frame',[caps[f] for f in capf])
capend=sorted(f for f,d in frames_end.items() if d.get('capture','0')!='0'); print('frame_end capture!=0',capend)
sd={int(r['frame']):r for r in st_rows}
for f in sorted(set(capf)|set(capend)):
    r=sd.get(f,{}); e=ef.get(f,{})
    print('cap',f,'nozzles',r.get('nozzles'),'ribbons',r.get('ribbons'),'live',r.get('ribbons_live'),'view',r.get('view_rule'),'suppressed',e.get('suppressed'),'records',e.get('records'))
print('other err/refus/fail heads',dict(other))
print('suppressed total',sum(int(d['suppressed']) for d in ef.values()),'forwarded_*',{k:sum(int(d[k]) for d in ef.values()) for k in next(iter(ef.values())) if k.startswith('forwarded')})
n401=max(int(kv(l)['frame']) for l in open(log('run401'),errors='replace') if l.startswith('frame_end '))
W=(500,min(n401,len(frames_end)-1)); print('dt window (capture frames excluded)',W)
for run in ['run403','run401']:
    a=dts(run,*W); print(run,'n',len(a),'median',st.median(a),'p90',pct(a,.9),'p99',pct(a,.99))
a=dts('run403',0,10**9); print('run403 whole session n',len(a),'median',st.median(a),'p90',pct(a,.9))
