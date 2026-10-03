#!/usr/bin/env python3
"""Run 407/408 engine-plumes triage: F8-frame engine_stage/engine_frame, small-parts cull census, engine_draw rows.
Usage: triage.py <session.log> ; prints compact tables only."""
import sys, re, math, statistics, collections
def kv(line):
    return dict(p.split('=',1) for p in line.split()[1:] if '=' in p)
path=sys.argv[1]
cap=set(); stage={}; frame={}; draws=[]; census=[]; cams={}; bounds=[]
st_us=[]; dts=[]; misc=collections.Counter(); health=[]
for line in open(path, errors='replace'):
    k=line.split(' ',1)[0]
    if k=='cull_census_frame': cap.add(int(kv(line)['frame']))
    elif k in('engine_stage','engine_frame','engine_draw','cull_census','camera_state','object_bounds','frame_end'):
        d=kv(line)
        if 'frame' not in d: continue
        f=int(d['frame'])
        if k=='engine_stage':
            stage[f]=d
            if d.get('ran')=='1': st_us.append(float(d['stage_us']))
        elif k=='engine_frame':
            frame[f]=d; misc['forwarded_stage_off']+=int(d.get('forwarded_stage_off',0))
        elif k=='engine_draw': draws.append(d)
        elif k=='cull_census': census.append(d)
        elif k=='camera_state': cams.setdefault(f,d)
        elif k=='object_bounds': bounds.append(d)
        elif k=='frame_end':
            for key in('dt_ms','dt','frame_ms'):
                if key in d: dts.append(float(d[key])); break
    elif k in('engine_plumes_state','engine_plumes_failed','session_end','engine_effects_plumes'):
        health.append(line.strip()[:220])
cap=sorted(cap)
print('F8 frames',cap)
SF=['records','nozzles','ships','floored','floor_unknown','culled_small','culled_behind','culled_rows','culled_idle','capped','faded','skipped_other_view','stage_us']
FF=['candidates','not_jet','records','suppressed','forwarded_unscoped','forwarded_snapshot','forwarded_opaque','forwarded_state','forwarded_overflow','forwarded_native','forwarded_stage_off','unknown_body','bodies','rows','rows_more']
print('engine_stage', ' '.join(SF))
for f in cap: print(f, ' '.join(stage.get(f,{}).get(x,'-') for x in SF))
print('engine_frame', ' '.join(FF))
for f in cap: print(f, ' '.join(frame.get(f,{}).get(x,'-') for x in FF))
# cull census per frame
print('cull_census verdicts per frame; engine bodies (name contains engine/fx_)')
for f in cap:
    rows=[c for c in census if int(c['frame'])==f]
    v=collections.Counter(c.get('verdict') for c in rows)
    eng=collections.Counter((c.get('verdict'),c.get('body','?')) for c in rows if re.search('engine|fx_|effects',c.get('body',''),re.I))
    print(f, dict(v), 'engine:', dict(eng) if eng else 'none')
allb=collections.Counter((c.get('verdict'),c.get('body','?')) for c in census if c.get('verdict')!='kept')
print('non-kept bodies over F8 frames (top 25):')
for (v,b),n in allb.most_common(25): print(' ',n,v,b)
print('census rows whose body mentions engines (any verdict):', sum(1 for c in census if re.search('engine',c.get('body',''),re.I)))
# engine_draw
print('engine_draw rows on F8 frames: frame model name lod size radius value_eff verdict dist(origin vs cam)')
camkeys=None
for d in draws:
    f=int(d['frame'])
    if f not in cap: continue
    c=cams.get(f,{}); dist='-'
    pos=c.get('position') or c.get('eye') or c.get('camera')
    if pos:
        try:
            p=[float(x) for x in pos.split(',')]; o=[float(x) for x in d['origin'].split(',')]
            dist='%.0f'%math.dist(p,o)
        except Exception: pass
    print(f, d.get('model'), d.get('name','').split('\\')[-1], 'lod='+d.get('lod','-'), 'size='+d.get('size','-'), 'R='+d.get('radius','-'), 'veff='+d.get('value_eff','-'), d.get('verdict'), 'handle='+d.get('handle','-'), 'dist='+dist)
# radius constancy per parent? engine_draw has handle (node handle); group by radius per handle
byh=collections.defaultdict(set)
for d in draws:
    if int(d['frame']) in cap: byh[d.get('handle')].add(d.get('radius'))
print('handles with >1 radius value on F8 frames:', {h:r for h,r in byh.items() if len(r)>1} or 'none')
print('floor_unknown>0 frames (whole session):', sum(1 for d in stage.values() if int(d.get('floor_unknown',0))>0))
print('engine_stage ran=1 frames', len(st_us), 'stage_us median', statistics.median(st_us) if st_us else '-')
print('forwarded_stage_off total', misc['forwarded_stage_off'])
print('frame_end dt median', statistics.median(dts) if dts else 'n/a (no dt field)')
print('camera_state keys sample:', sorted(next(iter(cams.values()),{}).keys())[:30])
print('object_bounds rows on F8 frames:', sum(1 for b in bounds if int(b['frame']) in cap), 'keys:', sorted(bounds[0].keys())[:30] if bounds else '-')
for h in health: print(h)
# --- jet bodies in the cull census (models 20399..20407, 566): verdict, projected s vs threshold, distance d
print('census jet rows per frame: frame kept culled_small other | culled s range | culled d range | kept d max')
JET=lambda c: (20399<=int(c['model'],16)<=20407) or int(c['model'],16)==566
for f in cap:
    rows=[c for c in census if int(c['frame'])==f and JET(c)]
    k=[c for c in rows if c['verdict']=='kept']; cs=[c for c in rows if c['verdict']=='culled_small']
    o=len(rows)-len(k)-len(cs)
    rng=lambda xs:('%s..%s'%(min(xs),max(xs))) if xs else '-'
    print(f,len(k),len(cs),o, rng([int(c['s']) for c in cs]), rng([int(c['d']) for c in cs]), rng([int(c['d']) for c in k]), 'thr='+(cs[0]['thr'] if cs else '-'))
# engine_draw summary per F8 frame
print('engine_draw summary per frame: model:count R values')
for f in cap:
    ds=[d for d in draws if int(d['frame'])==f]
    print(f, dict(collections.Counter(d['model'] for d in ds)), sorted(set(d.get('radius') for d in ds)), 'lod field present:', any('lod' in d for d in ds))
# --- join engine_draw node= to census node= for distance d and projected s; farthest ship-body node in the census
print('engine_draw joined to census (frame: model s d) and farthest ships\\ body node in census (any verdict)')
for f in cap:
    cn={c['node']:c for c in census if int(c['frame'])==f}
    j=[(d['model'],cn[d['node']]['s'],int(cn[d['node']]['d'])) for d in draws if int(d['frame'])==f and d.get('node') in cn]
    ships=[(int(c['d']),c['verdict'],c.get('body','')) for c in cn.values() if c.get('body','').startswith('ships') and c.get('body','').endswith(('hull','body')) or '\\hull' in c.get('body','')]
    far=max(ships) if ships else None
    print(f,'joined',len(j),'max d drawn',max((x[2] for x in j),default='-'),'s of drawn',sorted(set(x[1] for x in j)),'| hull nodes',len(ships),'farthest hull',far)
