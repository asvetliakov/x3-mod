# Run162 Q1-Q5: per capital member, its ship_gate_ctl rows (role / stage / key / petal) merged with its engine 0x12 warns (and the
# other), its order-8 launch (ship_gate_launch / the ctl launch row) and the gate's capital arrival launches, with game and real
# seconds. Usage: python3 -I sb_timeline.py rows.txt
import sys, os, re, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); R = C.defaultdict(list); ids = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame']); ids[d['ship']] = d['id']
        R[d['ship']].append((f, 'ctl %s gate=%s seq=%s key=%s order=%s %s>%s stage=%s petal=%s' % (d['why'], d['gate'][-4:], d['seq'], d['key'],
                              d['order'], d['from'], d['to'], d['stage'], d['petal'])))
    elif l.startswith('ship_avoid_engine_warn '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); R[d['obj']].append((int(d['frame']), '0x12 warn other=%s class=%s code=%s d_m=%s' % (d['other'], d['class'], d['code'], d['d_m'])))
    elif l.startswith('ship_gate_launch ') and ' capital=1 ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); R[d['ship']].append((int(d['frame']), 'ARRIVAL launch at %s kind=%s off_u_m=%s' % (d['gate'][-4:], d['kind'], d['off_u_m'])))
    elif l.startswith('ship_gate_approach ') and ' capital=1 ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); R[d['obj']].append((int(d['frame']), 'approach row cross_to_launch_ms=%s' % d.get('cross_to_launch_ms')))
for s in sorted(ids, key=lambda s: R[s][0][0]):
    print('== %s id=%s' % (s, ids[s]))
    for f, txt in sorted(R[s]):
        print('  f %6d game %8.1f real %5.1f | %s' % (f, tm.game(f) / 1000, tm.real(f) / 1000, txt))
