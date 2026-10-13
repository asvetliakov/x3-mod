"""Run161 Q1: every capital-vs-capital engine warn (ship_avoid_engine_warn_pre entry whose subject and other are capitals in caps.txt):
E verdict at the warn, both ships' controller role at that frame (ship_gate_ctl transitions), the pair's last ship_gate_ctl_pair row
before the warn, and whether a ship_avoid_engine_warn (order 0x12) row of the subject followed within 3 frames.
Also: per capital pair, ctl_pair rows by (verdict, before). Usage: python3 -I capcap_warns.py rows.txt"""
import sys, re, bisect, collections as C
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
role = C.defaultdict(list); pairs = C.defaultdict(list); pre = []; warn = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = KV(l); role[d['ship']].append((int(d['frame']), d['to'], d['gate'][-4:], d['seq']))
    elif l.startswith('ship_gate_ctl_pair '):
        d = KV(l); pairs[frozenset((d['a'], d['b']))].append((int(d['frame']), d['verdict'], d['before'], d['yielder'], d['cap_ms'], d['t_c_s']))
    elif l.startswith('ship_avoid_engine_warn_pre '):
        d = KV(l); ent = l.split('e_verdict=')[1].split()
        for e in ent:
            p = e.split(':')
            if d['obj'] in caps and p[0] in caps: pre.append((int(d['frame']), d['obj'], d['order'], p[0], p[3], p[6], p[8]))
    elif l.startswith('ship_avoid_engine_warn '):
        d = KV(l); warn[d['obj']].append(int(d['frame']))
def role_at(o, f):
    r = [x for x in role[o] if x[0] <= f]
    return '%s(%s seq%s @%d)' % (r[-1][1], r[-1][2], r[-1][3], r[-1][0]) if r else 'never'
vc = C.Counter()
print('frame A(class,order) B(class,order) d_m E_verdict | role A | role B | last pair row before | 0x12 followed')
for f, a, ao, b, bo, dm, v in pre:
    pr = [x for x in pairs[frozenset((a, b))] if x[0] <= f]
    fol = any(f <= w <= f + 3 for w in warn[a]); vc[(v, fol)] += 1
    print('%d %s(%s,%s) %s(%s,%s) %s %s | %s | %s | %s | %s' % (f, a, caps[a], ao, b, caps[b], bo, dm, v, role_at(a, f), role_at(b, f),
          pr[-1] if pr else 'none', fol))
print('capital-capital warn entries by (E verdict, 0x12 followed):', dict(vc))
print('== ctl_pair rows per capital pair: (verdict,before) counts')
for k, v in sorted(pairs.items(), key=lambda kv: kv[1][0][0]):
    if all(x in caps for x in k): print(sorted(k), dict(C.Counter((x[1], x[2]) for x in v)), 'frames %d-%d' % (v[0][0], v[-1][0]))
