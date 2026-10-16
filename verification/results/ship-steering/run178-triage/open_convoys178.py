# Run178 Q4: the convoys open at the log's end: per member its last ship_gate_ctl_member row (role, stage, order, target, gate-local
# position p_m and its distance, speed, nose) and its last ship_gate_ctl_lane row (lane_s, rank, gap, law, w), its ship_gate_ctl
# transitions, the 0x12 warn rows and ship_gate_ctl_pair verdicts naming it; the last traced game time.
# Usage: python3 -I open_convoys178.py ROWS GATE:SHIP,SHIP,SHIP ...
import sys, re, math, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
want = {}
for a in sys.argv[2:]:
    g, ss = a.split(':'); want[g] = ss.split(',')
ships = {s for v in want.values() for s in v}
mem = {}; lane = {}; ctl = C.defaultdict(list); x12 = C.Counter(); pair = C.Counter(); tlast = 0
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=\d+ t_ms=(\d+)', l); tlast = int(m[1]) / 1e3
    elif l.startswith('ship_gate_ctl_member '):
        d = kv(l)
        if d['ship'] in ships and d['gate'] in want: mem[d['ship']] = d
    elif l.startswith('ship_gate_ctl_lane '):
        d = kv(l)
        if d['ship'] in ships and d['gate'] in want: lane[d['ship']] = d
    elif l.startswith('ship_gate_ctl '):
        d = kv(l)
        if d['ship'] in ships: ctl[d['ship']].append('%.1f %s %s>%s %s' % (int(d['t_ms']) / 1e3, d['gate'][-4:], d['from'], d['to'], d['why']))
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l)
        if d['obj'] in ships: x12[d['obj']] += 1
    elif l.startswith('ship_gate_ctl_pair '):
        d = kv(l)
        for s in (d['a'], d['b']):
            if s in ships: pair[(s, d['verdict'], d['why'])] += 1
print('last traced game time %.1f' % tlast)
for g, ss in want.items():
    print('== gate', g)
    for s in ss:
        m = mem.get(s, {}); n = lane.get(s, {})
        p = [float(x) for x in m.get('p_m', '0,0,0').split(',')]
        print(' %s %s role=%s stage=%s order=%s target=%s p_m=%s |p|=%.0f v=%s nose_f=%s (t %.1f) | lane_s=%s rank=%s gap=%s law=%s w=%s on=%s | warn rows %d' % (
            s, m.get('class'), m.get('role'), m.get('stage'), m.get('order'), m.get('target'), m.get('p_m'), math.sqrt(sum(x * x for x in p)),
            m.get('v_ms'), m.get('nose_f'), int(m.get('t_ms', 0)) / 1e3, n.get('lane_s'), n.get('rank'), n.get('gap_m'), n.get('law'), n.get('w'), n.get('on'), x12[s]))
        print('   ctl:', '; '.join(ctl[s]))
        print('   pairs:', dict((k[1] + '/' + k[2], v) for k, v in pair.items() if k[0] == s))
