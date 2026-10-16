# Run178 Q1: our avoidance's touch on the four return-trip ships: per ship and order, traced rows, rows with point != given
# (the leaf point after our avoidance differs from the order's field), rows with obst != 0 and the obstacles; detour rows by
# (obj, obstacle, order) with time span; site E verdicts with A or B among the four, by verdict.
# Usage: python3 -I rt_writes178.py ROWS
import sys, re, collections as C
S = ['0x8fe612b8', '0x8fe58420', '0x39cfa520', '0x39cf2180']
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
n = C.Counter(); pg = C.Counter(); ob = C.Counter(); obs = C.defaultdict(C.Counter); det = C.defaultdict(list); ev = C.Counter(); f2t = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        d = kv(l); f2t.setdefault(int(d['frame']), int(d['t_ms']))
        if d['obj'] in S:
            k = (d['obj'], d['order']); n[k] += 1
            if d['point'] != d['given']: pg[k] += 1
            if d['obst'] != '0x00000000': ob[k] += 1; obs[k][d['obst']] += 1
    elif l.startswith('ship_avoid_detour '):
        d = kv(l)
        if d['obj'] in S: det[(d['obj'], d['obstacle'], d['order'])].append(int(d['frame']))
    elif l.startswith('ship_avoid_e_verdict '):
        d = kv(l)
        if d['A'] in S or d['B'] in S: ev[(('A' if d['A'] in S else 'B'), d['verdict'])] += 1
for k in sorted(n): print('trace %s order=%s rows %d point!=given %d obst!=0 %d %s' % (k[0], k[1], n[k], pg[k], ob[k], dict(obs[k])))
for k, fs in sorted(det.items()):
    print('detour obj=%s obstacle=%s order=%s rows %d frames %d-%d g=%.1f-%.1f' % (k + (len(fs), fs[0], fs[-1], f2t.get(fs[0], 0) / 1e3, f2t.get(fs[-1], 0) / 1e3)))
for k in sorted(ev): print('e_verdict', k, ev[k])
