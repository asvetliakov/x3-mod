#!/usr/bin/env python3
"""Run256 fog shadow-pass toggle triage: per-toggle-state fog frame fields and frame time."""
import re,sys,collections,statistics
L=sys.argv[1] if len(sys.argv)>1 else '/tmp/x3-bottleX3-run256/session-20260923-022616-212.log'
kv=re.compile(r'(\w+)=(\S+)')
toggles=[];fog={};fe={}
for line in open(L,errors='replace'):
    if line.startswith('fog_shadow_pass_toggle'):
        d=dict(kv.findall(line));toggles.append((int(d['frame']),int(d['enabled'])))
    elif line.startswith('volumetric_fog_frame '):
        d=dict(kv.findall(line));fog[int(d['frame'])]=d
    elif line.startswith('frame_end '):
        d=dict(kv.findall(line));fe[int(d['frame'])]=d
def state(f):
    s=1
    for tf,e in toggles:
        if tf<=f:s=e
    return s
print('toggles',len(toggles),'fog_frames',len(fog),'frame_end',len(fe))
if fe: print('frame_end keys',sorted(next(iter(fe.values())).keys()))
agg=collections.defaultdict(collections.Counter)
for f,d in fog.items():
    s=state(f)
    for k in ('applied','reason','grid_pass','grid_built','march','fallback','grid_refused','grid_cascades','sun','shadow_maps'):
        agg[(s,k)][d.get(k)]+=1
for key in sorted(agg):print(key,dict(agg[key].most_common(5)))
num=collections.defaultdict(list)
for f,d in fog.items():
    if d.get('applied')=='1':
        s=state(f)
        for k in ('grid_calls','grid_net_calls','cpu_us','grid_frame_term','grid_far_width'):
            try:num[(s,k)].append(float(d[k]))
            except:pass
for k in sorted(num):v=num[k];print(k,'n',len(v),'p50',statistics.median(v),'max',max(v))
dt=collections.defaultdict(list)
for f,d in fe.items():
    if f in fog and fog[f].get('applied')=='1': dt[state(f)].append(float(d['dt_ms']))
for s,v in sorted(dt.items()):
    v.sort();print('dt_ms state',s,'n',len(v),'p50',statistics.median(v),'p95',v[int(.95*len(v))])
allv=sorted(float(d['dt_ms']) for d in fe.values());print('dt_ms all n',len(allv),'p50',statistics.median(allv))
for cf in (5651,6395):print('capture frame',cf,'state',state(cf),'march',fog.get(cf,{}).get('march'))
