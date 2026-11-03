#!/usr/bin/env python3
"""Far-jet counter triage on an FL log (streaming). Usage: far_jets_triage.py LOG
Source semantics (HEAD 3b9eae65a, motion_output_engine_plumes_inc.h:964-967):
  far_jets      = handler Stats::written     (entries copied, engine_far_jets.cpp:92)
  far_records   = engine_far_.records         (stage records appended, plumes_inc.h:332)
  far_dropped   = handler steering (v/00566 model, engine_far_jets.cpp:47-49, never copied)
                + stage steering + stage invalid (far_record: scale 0 / non-finite / bad scale70/80; plumes_inc.h:318)
  far_duplicates= repeated (handle, view) pairs (plumes_inc.h:299)
Hence per row: jets - duplicates - ring_full - stage_steering - stage_invalid = records, and
dropped - (jets - duplicates - records) = handler steering (lower bound when ring_full>0)."""
import sys, re, collections
L = sys.argv[1]
CAP = {48727, 76463}
F = ('frame','far_jets','far_records','far_dropped','far_duplicates','far_engine','far_overflow','far_disarmed',
     'records','nozzles','skipped_other_view','far','node_records','steering')
pat = {k: re.compile(r'\b' + k + r'=(\d+)') for k in F}
rows = []; vr = collections.Counter(); capt = []
for line in open(L, errors='replace'):
    if not line.startswith('engine_stage '): continue
    d = {k: int(m.group(1)) for k in F if (m := pat[k].search(line))}
    m = re.search(r'view_rule=(\S+)', line); vr[m.group(1) if m else '?'] += 1
    rows.append(d)
    if d.get('frame') in CAP: capt.append(d)
n = len(rows); print('engine_stage rows', n, 'view_rule', dict(vr))
q = lambda v, p: sorted(v)[int(p*(len(v)-1))] if v else None
for k in ('far_jets','far_records','far_dropped','far_duplicates','far_engine','nozzles','skipped_other_view','far','node_records','records'):
    v = [r.get(k, 0) for r in rows]; print(f'{k:20s} p50={q(v,.5)} p90={q(v,.9)} max={max(v)} sum={sum(v)}')
J = [r['far_jets'] for r in rows]; R = [r['far_records'] for r in rows]; D = [r['far_dropped'] for r in rows]
Du = [r.get('far_duplicates',0) for r in rows]
print('rows records!=jets', sum(1 for r in rows if r['far_records'] != r['far_jets']))
print('rows records<jets-duplicates (stage invalid/steering/ring_full>0)',
      sum(1 for r in rows if r['far_records'] < r['far_jets'] - r.get('far_duplicates',0)))
stage_drop = sum(r['far_jets'] - r.get('far_duplicates',0) - r['far_records'] for r in rows)
print('stage-side dropped (invalid incl. scale0 + stage steering + ring_full) sum', stage_drop)
print('handler steering (dropped - stage-side) sum', sum(D) - stage_drop)
print('rows dropped>jets', sum(1 for a,b in zip(D,J) if a>b), 'dropped==0', sum(1 for a in D if a==0),
      'jets==0', sum(1 for a in J if a==0), 'jets==0&dropped>0', sum(1 for a,b in zip(D,J) if b==0 and a>0))
def corr(x, y):
    mx, my = sum(x)/len(x), sum(y)/len(y)
    sxy = sum((a-mx)*(b-my) for a,b in zip(x,y)); sx = sum((a-mx)**2 for a in x)**.5; sy = sum((b-my)**2 for b in y)**.5
    return sxy/(sx*sy) if sx and sy else float('nan')
print('corr(dropped,jets)=%.3f corr(dropped,engine)=%.3f corr(dropped,nozzles)=%.3f' %
      (corr(D,J), corr(D,[r.get('far_engine',0) for r in rows]), corr(D,[r.get('nozzles',0) for r in rows])))
for r in capt: print('capture', {k: r.get(k) for k in F})
# Q3: where the ring's records go (records = drawn nozzles + culls + skips), split by rows with/without far records.
G = ('records','nozzles','far','culled_small','culled_behind','culled_rows','culled_idle','skipped_other_view',
     'ribbon_skipped_other_view','far_records','node_records','capped','faded')
gp = {k: re.compile(r'\b' + k + r'=(\d+)') for k in G}
agg = collections.defaultdict(collections.Counter); nfar0_noz = 0; maxnoz_far = 0
for line in open(L, errors='replace'):
    if not line.startswith('engine_stage '): continue
    d = {k: int(m.group(1)) for k in G if (m := gp[k].search(line))}
    key = 'far_records>0' if d.get('far_records', 0) else 'far_records=0'
    agg[key]['rows'] += 1
    for k, v in d.items(): agg[key][k] += v
for key, c in agg.items(): print('Q3', key, dict(c))
