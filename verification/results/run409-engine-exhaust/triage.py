#!/usr/bin/env python3
"""Run 123 A (run409) numeric triage. Usage: python3 triage.py <session.log>. Streams once."""
import sys, re, collections
L = sys.argv[1]
kv = re.compile(r'(\w+)=(\S+)')
def num(v):
    try: return float(v)
    except: return None
ST = ['records','nozzles','far','far_jets','far_records','far_dropped','far_overflow','far_disarmed','floored','floor_unknown',
      'capped','faded','culled_small','culled_behind','culled_rows','culled_idle','skipped_other_view','stage_us','discs','attacks','attack_overflow',
      'view_own_total','view_majority_total','view_far_total','vertices']
LF = ['lights','ships','ships_drawn','nodes','candidates','draws_lit','records','invalid','orphan','ships_dropped','nodes_dropped','log_dropped','other_view']
SH = ['rects','drew','candidates','refused','capped','failed','cpu_us','stale_reverts']
seg = collections.defaultdict(lambda: collections.defaultdict(list))
viewrule = collections.defaultdict(collections.Counter)
light = collections.defaultdict(list); shim = collections.defaultdict(list)
shimon = collections.Counter(); shimskip = collections.Counter()
dt = collections.defaultdict(list); phases=[]
setaf = collections.Counter(); armed = collections.Counter()
bad = collections.Counter(); badex = {}
pat = re.compile(r'error|fail|refus|disarm|fallback|reset|lost|warn', re.I)
cur = 'default'; first_frame=None
for line in open(L, errors='replace'):
    t = line.split(' ',1)[0]
    if t == 'engine_stage':
        d = dict(kv.findall(line)); cur = d.get('preset', cur)
        armed[(d.get('armed'), d.get('reason'))] += 1
        if d.get('ran') != '1': continue
        s = seg[cur]
        for k in ST:
            if k in d: s[k].append(num(d[k]))
        viewrule[cur][d.get('view_rule')] += 1
        if d.get('seta') == '1': setaf['seta1'] += 1
        if d.get('seta') == '1' and num(d.get('travel','0')) > 0: setaf['seta1_travel'] += 1
        setaf['seta_refused_max'] = max(setaf['seta_refused_max'], int(d.get('seta_refused',0)))
        setaf['seta_invalid_max'] = max(setaf['seta_invalid_max'], int(d.get('seta_invalid',0)))
        setaf['read_'+d.get('seta_read','?')] += 1
        continue
    if t == 'frame_end':
        d = dict(kv.findall(line)); v = num(d.get('dt_ms','0'))
        if v and v > 0: dt[cur].append(v); dt['ALL'].append(v)
        continue
    if t == 'engine_light_frame':
        d = dict(kv.findall(line))
        for k in LF:
            if k in d: light[k].append(num(d[k]))
        continue
    if t == 'engine_shimmer':
        d = dict(kv.findall(line)); shimon[d.get('on')] += 1; shimskip[d.get('skipped','-')] += 1
        for k in SH:
            if k in d: shim[k].append(num(d[k]))
        continue
    if t == 'frame_phases':
        d = dict(kv.findall(line)); phases.append((num(d['dt_p50_us']), num(d['dt_p95_us']))); continue
    if t.endswith('_frame') or t in ('engine_frame',): 
        # per-frame counter rows: count only nonzero fail/refuse-type fields
        for k, v in kv.findall(line):
            if pat.search(k) and v not in ('0','0.0','0.000','00000000','00000001','none','-'):
                bad[t+':'+k] += 1
        continue
    if pat.search(line):
        bad[t] += 1; badex.setdefault(t, line[:220].rstrip())
def dist(a):
    a = sorted(x for x in a if x is not None)
    if not a: return '-'
    n=len(a); return f'n={n} min={a[0]:g} med={a[n//2]:g} p95={a[min(n-1,int(n*.95))]:g} max={a[-1]:g}'
for p, s in seg.items():
    print(f'## engine_stage ran=1 preset={p}')
    for k in ST:
        if s[k]: print(f'  {k}: {dist(s[k])}')
    print('  view_rule:', dict(viewrule[p]))
print('## engine_stage armed/reason', dict(armed)); print('## seta', dict(setaf))
print('## engine_light_frame'); [print(f'  {k}: {dist(light[k])}') for k in LF if light[k]]
print('## engine_shimmer on', dict(shimon), 'skipped', dict(shimskip)); [print(f'  {k}: {dist(shim[k])}') for k in SH if shim[k]]
print('## frame_end dt_ms'); [print(f'  {p}: {dist(v)}') for p, v in dt.items()]
print('## frame_phases dt_p50_us', dist([a for a,b in phases]), '| dt_p95_us', dist([b for a,b in phases]))
print('## suspicious rows/fields'); [print(f'  {k}: {v}  ex: {badex.get(k,"")}') for k, v in sorted(bad.items())]
