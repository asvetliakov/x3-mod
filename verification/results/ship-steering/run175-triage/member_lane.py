# Run175 Q2: a ship's member + lane rows at one gate, joined per frame (the lane row follows its member row), printed on every change
# of role/stage/order/rank/law/w/loop/on and at least every EVERY game seconds: game t, |p| (m, gate frame), p_m=<R>,<U>,<F>, nose_f,
# v, rank, gap, law, lat, v_law, v_w, w, on, merged, loop.  Usage: python3 -I member_lane.py ROWS SHIP GATE [EVERY_S]
import sys, re, math
rows, ship, gate = sys.argv[1:4]; every = float(sys.argv[4]) if len(sys.argv) > 4 else 10
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
m = None; last = None; lt = -1e9
for l in open(rows, errors='replace'):
    if not l.startswith('ship_gate_ctl_') or 'ship=' + ship not in l or 'gate=' + gate not in l: continue
    if l.startswith('ship_gate_ctl_member '): m = kv(l); continue
    if not l.startswith('ship_gate_ctl_lane ') or not m: continue
    d = kv(l); t = int(d['t_ms']) / 1000
    p = [float(x) for x in m['p_m'].split(',')]
    key = (m['role'], m['stage'], m['order'], d['rank'], d['law'], d['w'], d['loop'], d['on'])
    if key != last or t - lt >= every:
        print('f=%s g=%.1f %s/%s ord=%s |p|=%.0f p=%s nose_f=%s v=%s rank=%s gap=%s law=%s lat=%s v_law=%s v_w=%s w=%s on=%s mg=%s loop=%s' % (
            d['frame'], t, m['role'], m['stage'], m['order'], math.hypot(*p), m['p_m'], m['nose_f'], m['v_ms'], d['rank'], d['gap_m'], d['law'],
            d['lat_m'], d['v_law'], d['v_w'], d['w'], d['on'], d['merged'], d['loop']))
        last, lt = key, t
    m = None
