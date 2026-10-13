"""Run161: every ship_avoid_engine_warn row matched to the subject's ship_avoid_engine_warn_pre row (same obj, frame f-1/f/f-2): the
subject's order at the pre row vs the order at the warn row. Usage: python3 -I warn_subject_order.py rows.txt"""
import sys, re, collections as C
pre = {}; out = C.Counter(); W = []
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_avoid_engine_warn'): continue
    d = dict(re.findall(r'(\w+)=(\S+)', l))
    if l.startswith('ship_avoid_engine_warn_pre '): pre[(d['obj'], int(d['frame']))] = d['order']
    elif l.startswith('ship_avoid_engine_warn '): W.append((d['obj'], int(d['frame']), d['order']))
for o, f, od in W:
    k = next(((o, g) for g in (f - 1, f, f - 2) if (o, g) in pre), None)
    out[(pre[k] if k else 'no pre', od)] += 1
print('engine warn rows by (subject order at pre, order at warn row):', dict(out))
