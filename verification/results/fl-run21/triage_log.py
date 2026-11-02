#!/usr/bin/env python3
"""FL run 21 overlay triage (extends fl-run20/triage_log.py). Usage: triage_log.py LOG.
Fog state carried forward from volumetric_fog_frame applied=, cleared by volumetric_fog_sector profile=0.
Captures: capture_event frames grouped (gap<=2)."""
import re, sys, statistics as st
from collections import Counter
KV = re.compile(r'(\w+)=(\S+)')
BAD = re.compile(r'\b(\w*(?:refused|problem|fault|invalid|mismatch|rollback|disarm|orphan)\w*)=(\S+)')
KNOWN = {'hull_emission_frame','shadow_replay_candidates','thin_vote_frame','engine_frame','engine_light_frame','engine_shimmer'}
STATUS = ('lod_overlay','lod_strength','lod_material','lod_occlusion','terran_station_lod','original_fill_mode','original_fill_variant')
def pct(v,p):
    v=sorted(v); return v[min(len(v)-1,int(p*(len(v)-1)+0.5))] if v else None
def dist(v): return f"n={len(v)} min={min(v)} med={st.median(v)} p90={pct(v,.9)} max={max(v)}" if v else "n=0"
frames={}; fog=0; capfr=set(); bad=Counter(); badex={}; status=[]; fade=Counter(); fadenz=Counter(); fadeN=0
of=[]; firstpresent=None; first100=None
with open(sys.argv[1],errors='replace') as f:
    for line in f:
        k=line.split(' ',1)[0]
        if k=='frame_end':
            d=dict(KV.findall(line)); n=int(d['frame']); dr=int(d['draws'])
            frames[n]=(dr,int(d['dt_ms']),fog)
            if dr>100 and first100 is None: first100=(n,d['elapsed_ms'])
            continue
        if k in ('constant','sampler','state','stream','transform','texture','constants','vertex_element','texture_desc'): continue
        if k=='volumetric_fog_frame': fog=int(dict(KV.findall(line))['applied'])
        elif k=='volumetric_fog_sector' and ' profile=0 ' in line: fog=0
        elif k=='capture_event': capfr.add(int(dict(KV.findall(line))['frame']))
        elif k=='telemetry_first_present' and firstpresent is None: firstpresent=dict(KV.findall(line)).get('since_start_us')
        elif k=='fade_route_frame':
            d=dict(KV.findall(line)); fadeN+=1
            for x in ('fade_fill','fade_receiver','fade_caster','fade_gain','fade_widen','fade_routed','overlay_routed'):
                v=int(d.get(x,0)); fade[x]+=v; fadenz[x]+=v!=0
        elif k=='original_fill_frame': of.append(int(dict(KV.findall(line)).get('admitted',0)))
        if k.startswith(STATUS): status.append(line.strip()[:300])
        if k in KNOWN: continue
        for m in BAD.finditer(line):
            key,val=m.groups()
            if val in ('none','0','0.0','0.000','00000000'): continue
            if k=='window_mode' and 'reason=fullscreen' in line: continue
            s=f"{k}:{key}"; bad[s]+=1; badex.setdefault(s,line.strip()[:240])
caps=[]
for n in sorted(capfr):
    if caps and n-caps[-1][-1]<=2: caps[-1].append(n)
    else: caps.append([n])
print(f"log={sys.argv[1]} frames={len(frames)}")
print("== Q1 captures")
for i,c in enumerate(caps,1):
    n0=c[0]; win=[frames[j][1] for j in range(n0-30,n0+30) if j in frames]; dr=[frames[j][0] for j in c if j in frames]
    print(f"capture{i} frames={c[0]}-{c[-1]} draws={min(dr)}-{max(dr)} dt_median60={st.median(win)} fog={frames[n0][2]}")
print("fogged draws:",dist([v[0] for v in frames.values() if v[2]==1]))
print("clear draws(>20):",dist([v[0] for v in frames.values() if v[2]==0 and v[0]>20]))
print("fogged dt_ms:",dist([v[1] for v in frames.values() if v[2]==1]))
print("clear dt_ms(>20 draws):",dist([v[1] for v in frames.values() if v[2]==0 and v[0]>20]))
print("== Q2 status rows"); [print(" ",s) for s in status]
print("== Q3 nonzero flags (new kinds)")
for s,c in bad.most_common(): print(f"{c:8d} {s} ex: {badex[s]}")
print(f"== Q4 fade_route_frame rows={fadeN}"); [print(f"  {x} sum={fade[x]} nonzero_rows={fadenz[x]}") for x in fade]
print(f"original_fill_frame rows={len(of)} admitted: {dist(of)}")
print(f"== Q5 first_present since_start_us={firstpresent} first_frame_draws>100={first100}")
