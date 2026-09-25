"""Run336 shadow pop triage summaries. Usage: python3 analyze.py rows.json (from parse_rows.py)."""
import sys, json, statistics, collections
R = json.load(open(sys.argv[1]))
def I(d,k,default=0):
    try: return float(d.get(k, default))
    except: return default
def pct(v, p):
    v = sorted(v); return v[min(len(v)-1, int(p/100*len(v)))]
dep = R['shadow_replay_depth']; ret = R['shadow_retention_frame']; can = R['shadow_replay_candidates']
fr = [int(d['frame']) for d in dep]
print('Q0 depth frames', min(fr), max(fr), 'rows', len(dep))
caps = [128,512,1024,1024,1024]; recs=[1024,1024,2048,4096,4096]
print('Q1 per cascade max draws / cap / frames at cap')
for i in range(5):
    v = [I(d,f'draws{i}') for d in dep]
    m = max(v); at = [int(d['frame']) for d in dep if I(d,f'draws{i}')>=caps[i]]
    print(f'  c{i} max={m:.0f} cap={caps[i]} p50={pct(v,50):.0f} p99={pct(v,99):.0f} frames_at_cap={len(at)} argmax_frame={dep[v.index(m)]["frame"]}')
for key in ('skipped_caps','skipped_lease','skipped_state'):
    nz = [(d['frame'], d[key]) for d in dep if I(d,key)>0]
    print(f'  {key}>0 rows={len(nz)} first={nz[:5]}')
iss = [I(d,'issues') for d in dep]
print(f'  issues max={max(iss):.0f} p50={pct(iss,50):.0f} p99={pct(iss,99):.0f} budget=640 rows_issues>=640={sum(1 for x in iss if x>=640)}')
for key in ['capped']+[f'capped{i}' for i in range(5)]+[f'dropped_min_size{i}' for i in range(5)]+['overflow','footprint_refused0','footprint_refused1','footprint_refused2','footprint_refused3','footprint_refused4','stale','shadow_mismatch']:
    nz = [d['frame'] for d in can if I(d,key)>0]
    print(f'  cand {key}>0 rows={len(nz)} frames={nz[:6]}')
for i in range(5):
    nz = [d['frame'] for d in ret if I(d,f'capped_c{i}')>0]
    if nz: print(f'  ret capped_c{i}>0 rows={len(nz)} {nz[:6]}')
print('Q2 record list: candidates c<i> max vs records; routed max; retention records max')
for i in range(5):
    v=[I(d,f'c{i}') for d in can]; print(f'  c{i} max={max(v):.0f} records_cap={recs[i]}')
for k in ('routed','zwrite','managed','leased','excluded','fallback'):
    v=[I(d,k) for d in can]; print(f'  {k} max={max(v):.0f} p50={pct(v,50):.0f}')
v=[I(d,'records') for d in ret]; print(f'  retention records max={max(v):.0f}; nodes_live max={max(I(d,"nodes_live") for d in ret):.0f}')
print('Q3 retention')
modes = collections.Counter(d.get('mode') for d in ret); print('  modes', dict(modes))
for key in ('journal_overflow','moving_dropped','lod_replaced','model_replaced','reclassified','retired','superseded','evicted','box_exit','cam_jump','sun_relatch','refused','abandoned','deferred','buffer_gone','buffer_changed','unseen_in_frustum','records_unseen','nodes_unseen','flush'):
    if key=='flush':
        print('  flush', dict(collections.Counter(d.get('flush') for d in ret))); continue
    nz=[(d['frame'],d.get(key)) for d in ret if I(d,key)>0]
    print(f'  {key}>0 rows={len(nz)} max={max([float(x[1]) for x in nz],default=0):.0f} first={nz[:8]}')
# last frame with records>0
live = [int(d['frame']) for d in ret if I(d,'records')>0]
print('  retention frames with records>0', len(live), 'range', min(live), max(live))
print('  retention frame range', ret[0]['frame'], ret[-1]['frame'])
print('Q4 far cadence')
far = [int(d['frame']) for d in dep if I(d,'far_replayed')>0]
print('  far_replayed=1 rows', len(far), 'of', len(dep))
gaps = collections.Counter(b-a for a,b in zip(far,far[1:])); print('  gaps', gaps.most_common(8))
ff = collections.Counter(int(d['frame'])-int(d.get('far_frame',d['frame'])) for d in dep); print('  frame-far_frame', ff.most_common(8))
print('Q5 us distribution')
for key in ('us','apply_us'):
    v=[I(d,key) for d in dep]; print(f'  {key} n={len(v)} p50={pct(v,50):.1f} p90={pct(v,90):.1f} p99={pct(v,99):.1f} max={max(v):.1f} mean={statistics.mean(v):.1f}')
v=[I(d,'us') for d in ret]; print(f'  retention us p50={pct(v,50):.1f} p99={pct(v,99):.1f} max={max(v):.1f}')
v=[I(d,'select_us') for d in can]; print(f'  select_us p50={pct(v,50):.1f} p99={pct(v,99):.1f} max={max(v):.1f}')
dr=[I(d,'draws') for d in dep]; print(f'  draws p50={pct(dr,50):.0f} p99={pct(dr,99):.0f} max={max(dr):.0f}')
