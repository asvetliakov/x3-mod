#!/usr/bin/env python3
"""FL run 23 triage (adds Q2 load-matched dt bins and Q4 bolt sums; --bolts-only for AP). FL run 22 triage (extends fl-run21/triage_log.py): culls, material strength, draws/issued. Usage: triage_log.py LOG.
Fog state carried forward from volumetric_fog_frame applied=, cleared by volumetric_fog_sector profile=0.
Captures: capture_event frames grouped (gap<=2). frame_end draws= is every application draw, issued= those that reached
the device (src/proxy/capture.cpp frame_end row), so a cull shows as draws-issued, not as lower draws."""
import re, sys, statistics as st
from collections import Counter
KV = re.compile(r'(\w+)=(\S+)')
BAD = re.compile(r'\b(\w*(?:refused|problem|fault|invalid|mismatch|rollback|disarm|orphan|unresolved|failed)\w*)=(\S+)')
KNOWN = {'hull_emission_frame','shadow_replay_candidates','thin_vote_frame','engine_frame','engine_light_frame','engine_shimmer','engine_light_variant'}
KNOWN_KEY = {('emission_source_gain_frame','refused_other')}
SKIPK = ('constant','sampler','state','stream','transform','texture','constants','vertex_element','texture_desc')
def pct(v,p):
    v=sorted(v); return v[min(len(v)-1,int(p*(len(v)-1)+0.5))] if v else None
def dist(v): return f"n={len(v)} min={min(v)} med={st.median(v)} p90={pct(v,.9)} max={max(v)}" if v else "n=0"
frames={}; fog=0; capfr=set(); bad=Counter(); badex={}; att0=[]; reasons=Counter(); reasonex={}
props=[]; occ=[]; occf=[]; strength_tex=[]; strength_frame=[]; strength_var=[]; once=[]
ONCE=('cull_small_props_device','cull_small_props ','occlusion_cull_device','occlusion_cull_config','lod_material','lod_strength_session')
with open(sys.argv[1],errors='replace') as f:
    for line in f:
        k=line.split(' ',1)[0]
        if k=='frame_end':
            d=dict(KV.findall(line)); n=int(d['frame'])
            frames[n]=(int(d['draws']),int(d['dt_ms']),fog,int(d.get('issued',-1)))
            continue
        if k in SKIPK: continue
        if k=='volumetric_fog_frame': fog=int(dict(KV.findall(line))['applied'])
        elif k=='volumetric_fog_sector' and ' profile=0 ' in line: fog=0
        elif k=='capture_event': capfr.add(int(dict(KV.findall(line))['frame']))
        elif k=='cull_small_props_frame': props.append(dict(KV.findall(line)))
        elif k=='occlusion_cull_session': occ.append(dict(KV.findall(line)))
        elif k=='occlusion_cull': d=dict(KV.findall(line)); occf.append((int(d['frame']),d))
        elif k=='lod_strength_texture': strength_tex.append(dict(KV.findall(line)))
        elif k=='lod_strength_frame': strength_frame.append(dict(KV.findall(line)))
        elif k=='lod_strength_variant': strength_var.append(dict(KV.findall(line)))
        if line.startswith(ONCE): once.append(line.strip()[:260])
        if k in KNOWN: continue
        if ' attached=0' in line: att0.append(line.strip()[:240])
        m=re.search(r'\breason=(\S+)',line)
        if m and m.group(1) not in ('ok','none','change','cadence','-') and not (k=='window_mode' and m.group(1)=='fullscreen'):
            s=f"{k}:reason={m.group(1)}"; reasons[s]+=1; reasonex.setdefault(s,line.strip()[:240])
        for m in BAD.finditer(line):
            key,val=m.groups()
            if val in ('none','0','0.0','0.000','00000000','0,0,0') or (k,key) in KNOWN_KEY: continue
            if k=='window_mode' and 'reason=fullscreen' in line: continue
            s=f"{k}:{key}"; bad[s]+=1; badex.setdefault(s,line.strip()[:240])
caps=[]
for n in sorted(capfr):
    if caps and n-caps[-1][-1]<=2: caps[-1].append(n)
    else: caps.append([n])
print(f"log={sys.argv[1]} frames={len(frames)}")
print("== Q1 captures (draws=application draws, issued=reached device)")
for i,c in enumerate(caps,1):
    n0=c[0]; win=[frames[j][1] for j in range(n0-30,n0+30) if j in frames]
    dr=[frames[j][0] for j in c if j in frames]; iss=[frames[j][0]-frames[j][3] for j in c if j in frames and frames[j][3]>=0]
    print(f"capture{i} frames={c[0]}-{c[-1]} draws={min(dr)}-{max(dr)} not_issued={min(iss) if iss else '-'}-{max(iss) if iss else '-'} dt_median60={st.median(win)} fog={frames[n0][2]}")
for lab,sel in (("fogged",lambda v:v[2]==1),("clear(>20)",lambda v:v[2]==0 and v[0]>20)):
    vs=[v for v in frames.values() if sel(v)]
    print(f"{lab} draws:",dist([v[0] for v in vs]))
    print(f"{lab} draws-issued:",dist([v[0]-v[3] for v in vs if v[3]>=0]))
    print(f"{lab} dt_ms:",dist([v[1] for v in vs]))
print("== once rows"); [print(" ",s) for s in once]
print(f"== Q2 lod_strength_texture rows={len(strength_tex)} distinct_textures={len({d['texture'] for d in strength_tex})}")
print("  (slot,lane,k,payload,stored,size):",dict(Counter((d['slot'],d['lane'],d['k'],d['payload'],d['stored'],d['size']) for d in strength_tex)))
if strength_frame:
    last=strength_frame[-1]; nz=[d for d in strength_frame if d['no_twin']!='0' or d['refused']!='0' or d['failed']!='0']
    print(f"  lod_strength_frame rows={len(strength_frame)} seen_first={strength_frame[0]['seen']} seen_max={max(strength_frame,key=lambda d:int(d['seen'].split(',')[0]))['seen']} table_max={max(int(d['table']) for d in strength_frame)} twins_last={last['twins']} session_draws_last={last['session_draws']} rows_with_no_twin/refused/failed={len(nz)}")
    print(f"  lod_strength_variant rows={len(strength_var)} forms={dict(Counter(d['form'] for d in strength_var))} refusals={dict(Counter(d['refusal'] for d in strength_var))}")
print(f"== Q3 cull_small_props_frame rows={len(props)}")
F=('draws','culled','kept','kept_size','no_bounds','unbounded','unresolved','deferred','exempt_own','exempt_target','no_scope','resolves','walks','frames')
if props:
    tot={x:sum(int(d[x]) for d in props) for x in F}; print("  totals:",tot)
    print("  draws per window:",dict(Counter(d['draws'] for d in props)),"no_scope per window:",dist([int(d['no_scope']) for d in props]))
    print("  resolves per window:",dist([int(d['resolves']) for d in props]))
print(f"== Q4 occlusion_cull_session rows={len(occ)} (cumulative)")
G=('frames','candidates','tested','hidden','skipped','ready','not_ready','unstable','no_hull','no_bounds','unbounded','retest_skipped','pool_truncated','errors','drawn_late','refused','failed','state','blocks','resolves','walks')
for d in occ[:1]+occ[len(occ)//2:len(occ)//2+1]+occ[-1:]:
    print("  frame",d['frame'],{x:d.get(x) for x in G})
if occf:
    for lab,sel in (("fogged",1),("clear",0)):
        rows=[d for n,d in occf if n in frames and frames[n][2]==sel]
        print(f"  per-frame occlusion_cull rows {lab}: n={len(rows)}",{x:sum(int(d.get(x,0)) for d in rows) for x in ('candidates','tested','hidden','skipped','no_hull','retest_skipped','unstable','no_bounds')})
    for i,c in enumerate(caps,1):
        rows=[d for n,d in occf if c[0]<=n<=c[-1]]
        print(f"  capture{i} occlusion_cull per frame:",[(d['candidates'],d['tested'],d['hidden'],d['skipped'],d['no_hull']) for d in rows[:3]])
print("== Q5 attached=0 rows:",len(att0)); [print("  ",s) for s in att0[:10]]
print("== Q5 reason= not ok:"); [print(f"{c:8d} {s} ex: {reasonex[s]}") for s,c in reasons.most_common()]
print("== Q5 nonzero flags (new kinds)")
for s,c in bad.most_common(): print(f"{c:8d} {s} ex: {badex[s]}")
print("== A/B load bins (draws>20, frame>=300, dt_ms<=100)")
inf={n:v for n,v in frames.items() if n>=300 and v[0]>20 and v[1]<=100}
def q(v): return f"n={len(v)} p50={pct(v,.5)} p90={pct(v,.9)}" if v else "n=0"
for lab,sel in (("clear",0),("fogged",1)):
    vs=[v for v in inf.values() if v[2]==sel]
    print(f"{lab} overall dt: {q([v[1] for v in vs])}")
    for key,idx in (("issued",3),("draws",0)):
        b={}
        for v in vs: b.setdefault(v[idx]//20*20,[]).append(v[1])
        for k in sorted(b):
            if len(b[k])>=20: print(f"  {lab} {key}[{k},{k+20}) {q(b[k])}")
print("== bolts")
