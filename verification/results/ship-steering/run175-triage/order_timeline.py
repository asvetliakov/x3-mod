# Run174 Q2/Q3 (reused for Run175 Q2): a ship's order timeline from every row that carries its live order (ship_trace order=, ship_gate_ctl_member order=,
# ship_avoid_e_verdict A_order=, ship_avoid_engine_warn(_pre) order=), one line per change, with game time (t_ms carried forward
# from any row with t_ms), the source row and the duration of the previous order run.
# Usage: python3 -I order_timeline.py ROWS OBJ [LO_FRAME HI_FRAME]
import sys, re
rows, obj = sys.argv[1], sys.argv[2]; lo = int(sys.argv[3]) if len(sys.argv) > 3 else 0; hi = int(sys.argv[4]) if len(sys.argv) > 4 else 1 << 30
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
tms = 0; prev = None; t0 = None; f0 = None; n = 0
for l in open(rows, errors='replace'):
    if not l.startswith('ship_'): continue
    m = re.search(r' t_ms=(\d+)', l)
    if m: tms = int(m[1])
    d = None
    if l.startswith('ship_trace frame=') and 'obj=' + obj in l: d = kv(l); o, src, tg = d['order'], 'trace', d['tgt']
    elif l.startswith('ship_gate_ctl_member ') and 'ship=' + obj in l: d = kv(l); o, src, tg = d['order'], 'member', d['target']
    elif l.startswith('ship_avoid_e_verdict ') and 'A=' + obj in l: d = kv(l); o, src, tg = d['A_order'], 'e_verdict', '?'
    elif l.startswith('ship_avoid_engine_warn') and 'obj=' + obj in l: d = kv(l); o, src, tg = d['order'], l.split()[0][10:], d.get('tgt', '?')
    if not d: continue
    f = int(d['frame'])
    if not lo <= f <= hi: continue
    if o != prev:
        if prev is not None: print('   (%s for %.1f s game, %d frames)' % (prev, (tms - t0) / 1000, f - f0))
        print('f=%d g=%.1f order=%s tgt=%s src=%s' % (f, tms / 1000, o, tg, src)); prev, t0, f0 = o, tms, f
print('   (%s to f=%d g=%.1f)' % (prev, f, tms / 1000))
