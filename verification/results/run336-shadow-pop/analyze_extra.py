"""Run336: footprint gate, caster-set flips, frames without a depth replay, flush position. Usage: analyze_extra.py rows.json"""
import sys,json,collections
R=json.load(open(sys.argv[1]))
f=lambda d,k: float(d.get(k,0) or 0)
def pct(v,p): v=sorted(v); return v[min(len(v)-1,int(p/100*len(v)))]
can=R['shadow_replay_candidates']; dep=R['shadow_replay_depth']; ret=R['shadow_retention_frame']
for i in range(5):
    v=[f(d,f'footprint_refused{i}') for d in can]; a=[f(d,f'footprint_aged{i}') for d in can]
    fl=[f(d,f'flip_c{i}') for d in can]; p2=[f(d,f'period2_c{i}') for d in can]
    print(f'c{i} footprint_refused p50={pct(v,50):.0f} p99={pct(v,99):.0f} max={max(v):.0f} | aged>0 rows={sum(1 for x in a if x>0)} | flip>0 rows={sum(1 for x in fl if x>0)} flip p50={pct(fl,50):.0f} p99={pct(fl,99):.0f} max={max(fl):.0f} | period2>0 rows={sum(1 for x in p2 if x>0)} max={max(p2):.0f}')
dfr={int(d['frame']) for d in dep}; cfr={int(d['frame']):d for d in can}
miss=sorted(x for x in cfr if x not in dfr)
print('frames with candidates but no depth row', len(miss))
runs=[];s=None;p=None
for x in miss:
    if p is None or x!=p+1: 
        if s is not None: runs.append((s,p))
        s=x
    p=x
if s is not None: runs.append((s,p))
print(' runs', runs[:20])
print(' leased in missing frames', collections.Counter(cfr[x].get('leased') for x in miss).most_common(5))
print(' zwrite in missing frames', collections.Counter(cfr[x].get('zwrite') for x in miss).most_common(5))
