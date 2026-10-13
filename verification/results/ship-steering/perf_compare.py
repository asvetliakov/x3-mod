#!/usr/bin/env python3
"""Per-log aggregates of ship_steering_summary rows plus frame_end dt_ms.
Usage: python3 -I perf_compare.py > perf_compare_run159_161.txt"""
import re,os,sys,math
D=os.path.dirname(os.path.abspath(__file__))
LOGS=['run159','run160','run160b','run161']
KV=re.compile(r'(\w+)=([^\s,]+)')
def q(v,p):
    if not v: return float('nan')
    v=sorted(v); return v[min(len(v)-1,int(round(p*(len(v)-1))))]
def dist(v): return "p50=%.2f p90=%.2f p99=%.2f max=%.2f"%(q(v,.5),q(v,.9),q(v,.99),max(v) if v else float('nan'))
def num(s):
    try: return float(s)
    except: return None
for r in LOGS:
    path=f"{D}/{r}-triage/x3m.log"
    rows=[];dts={}
    fe=re.compile(r'^frame_end device=\d+ frame=(\d+) .*?dt_ms=(\d+)')
    for l in open(path,errors='replace'):
        if l.startswith('frame_end '):
            m=fe.match(l)
            if m: dts[int(m.group(1))]=int(m.group(2))
        elif l.startswith('ship_steering_summary '):
            rows.append({k:v for k,v in KV.findall(l)})
    print(f"=== {r}: {len(rows)} summary rows, {len(dts)} frame_end rows")
    us=sorted({k for x in rows for k in x if '_us' in k})
    print("us fields:",", ".join(us))
    for k in ['handler_us_p50','handler_us_p99','handler_us_max']:
        v=[num(x[k]) for x in rows if k in x and num(x[k]) is not None]
        print(f"  {k}: {dist(v)}")
    for k in us:
        if k.startswith('handler_'): continue
        v=[num(x[k]) for x in rows if k in x and num(x[k]) is not None]
        print(f"  {k}: max={max(v) if v else 'NA'} (rows with value>0: {sum(1 for a in v if a>0)})")
    for k in ['calls','ships','walked','snapshots','e_calls','timed','interval_ms']:
        v=[num(x[k]) for x in rows if k in x and num(x[k]) is not None]
        print(f"  {k}: p50={q(v,.5):.0f} max={max(v) if v else 'NA'}")
    print("  note: SETA share field: none in summary rows")
    bud=[];budf=[];fdt=[];pairs=[]
    prev=None
    for x in rows:
        c=num(x.get('calls','0')) ;p50=num(x.get('handler_us_p50','0'));iv=num(x.get('interval_ms','0'));fr=int(x.get('frame','0'))
        if iv and p50 is not None:
            bud.append(c*p50/(iv*1000)*100)
            if prev is not None and fr>prev:
                budf.append(c*p50/(fr-prev))
        if prev is not None and fr>prev:
            w=[dts[f] for f in range(prev+1,fr+1) if f in dts]
            if w:
                fdt.extend(w); pairs.append((sum(w)/len(w),p50 if p50 is not None else 0,num(x.get('handler_us_max','0')) or 0))
        prev=fr
    print(f"  budget calls*p50/interval, % of wall time: p50={q(bud,.5):.5f} max={max(bud):.5f}")
    print(f"  budget us per frame (calls*p50/frames in interval): p50={q(budf,.5):.3f} max={max(budf):.3f}")
    print(f"  frame dt_ms (all frames in windows): n={len(fdt)} p50={q(fdt,.5)} p90={q(fdt,.9)} p99={q(fdt,.99)} max={max(fdt) if fdt else 'NA'}")
    if len(pairs)>2:
        def corr(a,b):
            n=len(a);ma=sum(a)/n;mb=sum(b)/n
            sa=math.sqrt(sum((x-ma)**2 for x in a));sb=math.sqrt(sum((x-mb)**2 for x in b))
            return sum((x-ma)*(y-mb) for x,y in zip(a,b))/(sa*sb) if sa*sb else float('nan')
        print(f"  corr(interval mean dt_ms, handler_us_p50)={corr([p[0] for p in pairs],[p[1] for p in pairs]):.3f}  corr(.., handler_us_max)={corr([p[0] for p in pairs],[p[2] for p in pairs]):.3f}  n={len(pairs)}")
