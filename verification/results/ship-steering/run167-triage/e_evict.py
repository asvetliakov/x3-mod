# Run164 Q3/Q4: (a) site E's rows for each evicted escort against the 0x12's other in the 400 frames before the eviction (verdict, d_m,
# step_ms); (b) every E row against a gate (B_class 18) by (A is a member?, A's stage at the frame, verdict, gate_row), and the step_ms max;
# (c) pass_not_fresh rows by (A member?, B member?, B_class). Usage: python3 -I e_evict.py rows.txt
import sys, re, collections as C
ev = C.defaultdict(list); E = []; WARN = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ev[d['ship']].append((int(d['frame']), d['to'], d['stage'].split('>')[1], d['why'], d['gate'], d['id']))
    elif l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); E.append(d)
    elif l.startswith('ship_avoid_engine_warn '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); WARN.append(d)
def st(s, f):
    x = [r for r in ev.get(s, []) if r[0] <= f]; return (x[-1][1], x[-1][2]) if x else ('none', 'none')
print('(a) evictions')
for s, rows in ev.items():
    for (f, to, stage, why, gate, i) in rows:
        if why != 'evict': continue
        w = [x for x in WARN if x['obj'] == s and f - 50 <= int(x['frame']) <= f]
        o = w[-1]['other'] if w else None
        rr = [x for x in E if x['A'] == s and x['B'] == o and f - 400 <= int(x['frame']) <= f]
        rb = [x for x in E if x['B'] == s and x['A'] == o and f - 400 <= int(x['frame']) <= f]
        print(' id %s %s f %d stage %s | 0x12 other %s class %s code %s d_m %s r_m %s | E A=evictee vs other: %s | E A=other vs evictee: %s' % (i, s, f, stage, o,
              w[-1]['class'] if w else '-', w[-1].get('code') if w else '-', w[-1]['d_m'] if w else '-', w[-1]['r_m'] if w else '-',
              dict(C.Counter((x['A_order'], x['verdict']) for x in rr)), dict(C.Counter((x['A_order'], x['verdict']) for x in rb))))
print('(b) E rows vs gates by (A member role, stage, verdict, gate_row)')
c = C.Counter(); smax = C.Counter()
for x in E:
    if x['B_class'] != '18': continue
    role, stage = st(x['A'], int(x['frame'])); c[(role, stage, x['A_order'], x['verdict'], x.get('gate_row', '-'))] += 1
for k, n in sorted(c.items(), key=lambda kv: -kv[1]): print('  %5d %s' % (n, k))
print('(c) pass_not_fresh rows by (A role/stage, B role/stage, B_class), step_ms max')
c = C.Counter(); mx = 0
for x in E:
    if x['verdict'] != 'pass_not_fresh': continue
    f = int(x['frame']); c[(st(x['A'], f), st(x['B'], f), x['B_class'])] += 1; mx = max(mx, int(x.get('step_ms', 0)))
for k, n in sorted(c.items(), key=lambda kv: -kv[1]): print('  %5d %s' % (n, k))
print('  step_ms max on those rows', mx, '| step_ms max on all E rows', max(int(x.get('step_ms', 0)) for x in E))
print('(d) gate_row=stale rows', sum(1 for x in E if x.get('gate_row') == 'stale'), '| funnel/handover/dock members on E vs own gate not suppressed:',
      sum(1 for x in E if x['B_class'] == '18' and st(x['A'], int(x['frame']))[1] in ('funnel', 'handover', 'dock') and not x['verdict'].startswith('suppress')))
