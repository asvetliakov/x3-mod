# Run169 Q2: ship_avoid_engine_warn rows (the engine's own avoid order writes) on gate-controller members: obj and other with ids,
# class, member role/stage/order/gate of both at the warn (last 1 Hz member row), d_m, r_m. Capital-vs-capital member pairs flagged.
# Usage: python3 -I x12_warn.py rows.txt
import sys, re
ids = {}; last = {}; tm = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('frame_end '):
        pass
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); last[d['ship']] = (int(d['t_ms']), d['role'], d['stage'], d['order'], d['gate'][-4:], d['class'])
        tm['t'] = int(d['t_ms'])
    elif l.startswith('ship_avoid_engine_warn '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); a, b = last.get(d['obj']), last.get(d['other'])
        if not (a or b): continue
        f = lambda r: '%s %s %s %s %s' % (r[5], r[1], r[2], r[3], r[4]) if r else '-'
        print('f%s ~game %.1f | %s [%s] | other %s [%s] code %s | d_m %s r_m %s | count %s | cap-cap members %s' % (d['frame'], tm.get('t', 0) / 1000, ids.get(d['obj'], d['obj']), f(a),
              ids.get(d['other'], d['other']), f(b), d['code'], d['d_m'], d['r_m'], d['count'], bool(a and b)))
