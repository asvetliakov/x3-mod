#!/usr/bin/env python3
"""Run 118 A triage: engine_frame/engine_draw/selector/frame_end aggregates.
Usage: triage.py <session.log> [<session.log> ...]  (streams, prints aggregates only)"""
import sys, re, statistics as st
from collections import Counter
def kv(line):
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line))
for path in sys.argv[1:]:
    print("==", path)
    recs=[]; tot=Counter(); ovf=0; draws=[]; sel=Counter(); dts=[]; frames_sel={}
    errs=Counter(); eng_names=Counter()
    with open(path,'rb') as fh:
        for raw in fh:
            line=raw.decode('latin1').rstrip('\n')
            name=line.split(' ',1)[0]
            if 'engine' in name: eng_names[name]+=1
            if re.search(r'error|fail|late_claim|refus', name): errs[name]+=1
            if name=='engine_frame':
                d=kv(line); recs.append(int(d.get('records',0)))
                for k,v in d.items():
                    if k.startswith(('forwarded_','order_')) or k in('suppressed','unknown_body','rows_unknown','candidates','not_jet','steering','rows_more'):
                        try: tot[k]+=int(v)
                        except ValueError: pass
                tot['pinned='+d.get('pinned','?')]+=1
                if int(d.get('forwarded_overflow',0))>0: ovf+=1
            elif name=='engine_draw':
                draws.append(kv(line))
            elif name=='motion_output_frame':
                d=kv(line)
                sel[(d.get('selector_state'),d.get('scene_open'),d.get('scene_end_source'),d.get('scene_end_check'),d.get('scene_hook'))]+=1
            elif name=='frame_end':
                d=kv(line); dt=float(d.get('dt_ms',0)); f=int(d.get('frame',0))
                if dt>0: dts.append((f,dt))
    r=sorted(recs)
    print("engine_frame rows",len(r),"records min/median/max",r[0],st.median(r),r[-1],"frames_with_records",sum(1 for x in r if x),"overflow_frames",ovf)
    for k in sorted(tot): print("  total",k,tot[k])
    print("engine_draw rows",len(draws),"frames",sorted({int(d['frame']) for d in draws}))
    names=Counter((d.get('name'),d.get('model')) for d in draws)
    for (n,m),c in names.most_common(): 
        zs=[float(d['z']) for d in draws if d.get('name')==n]; ss=[float(d['s']) for d in draws if d.get('name')==n]
        print(f"  {c:4d} model={m} {n} z {min(zs):.3f}..{max(zs):.3f} s {min(ss):.3f}..{max(ss):.3f}")
    for fr in sorted({int(d['frame']) for d in draws}):
        zs=[float(d['z']) for d in draws if int(d['frame'])==fr]
        print(f"  frame {fr}: rows {len(zs)} z {min(zs):.3f}..{max(zs):.3f} median {st.median(zs):.3f}")
    for key in ('pair','flags130','order','blend','src','dst','zwrite','scope_depth','handle','cluster','verdict'):
        print("  ",key,dict(Counter(d.get(key,'<absent>') for d in draws)))
    print("  node absent/zero:",sum(1 for d in draws if d.get('node') in (None,'0','00000000')),"body unknown:",sum(1 for d in draws if d.get('body') in (None,'-','unknown')))
    print("selector tuples (selector_state,scene_open,end_source,end_check,scene_hook):")
    for t,c in sel.most_common(8): print("  ",c,t)
    print("frame_end dt_ms rows",len(dts))
    for lo,hi in ((1000,1200),(1200,1500),(1500,2000),(2000,5000),(5000,9000),(9000,13000)):
        w=[dt for f,dt in dts if lo<=f<hi]
        if w: print(f"  frames {lo}-{hi}: n={len(w)} median_dt={st.median(w):.2f} p90={sorted(w)[int(.9*len(w))]:.2f}")
    print("engine row names",dict(eng_names)); print("error/refusal row names",dict(errs))
