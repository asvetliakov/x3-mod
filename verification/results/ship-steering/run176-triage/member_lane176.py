# Run176 Q1-Q4 (run175-triage/member_lane.py plus the Run175 round's fields): a ship's member + lane rows at one gate joined per frame,
# printed on every change of role/stage/order/rank/law/w/loop/on/w_ok/funnel/pt/own and at least every EVERY game s: game t, |p|,
# p_m=<R>,<U>,<F> (gate frame), nose_f, v, rank, gap, law, lat, v_law, v_w, w, on, merged, loop, w_m, w_ok, funnel, pt, own.
# Usage: python3 -I member_lane176.py ROWS SHIP GATE [EVERY_S] [T_LO_S T_HI_S]
import sys, re, math
rows, ship, gate = sys.argv[1:4]; every = float(sys.argv[4]) if len(sys.argv) > 4 else 10
lo, hi = (float(sys.argv[5]), float(sys.argv[6])) if len(sys.argv) > 6 else (-1e18, 1e18)
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
m = None; last = None; lt = -1e9
for l in open(rows, errors='replace'):
    if not l.startswith('ship_gate_ctl_') or 'ship=' + ship not in l or 'gate=' + gate not in l: continue
    if l.startswith('ship_gate_ctl_member '): m = kv(l); continue
    if not l.startswith('ship_gate_ctl_lane ') or not m: continue
    d = kv(l); t = int(d['t_ms']) / 1000
    if not lo <= t <= hi: m = None; continue
    p = [float(x) for x in m['p_m'].split(',')]
    key = (m['role'], m['stage'], m['order'], d['rank'], d['law'], d['w'], d['loop'], d['on'], m.get('w_ok'), m.get('funnel'), d.get('pt'), d.get('own'))
    if key != last or t - lt >= every:
        print('f=%s g=%.1f %s/%s ord=%s |p|=%.0f p=%s nose_f=%s v=%s rank=%s gap=%s law=%s lat=%s v_law=%s v_w=%s w=%s on=%s mg=%s loop=%s w_m=%s w_ok=%s fun=%s pt=%s own=%s' % (
            d['frame'], t, m['role'], m['stage'], m['order'], math.hypot(*p), m['p_m'], m['nose_f'], m['v_ms'], d['rank'], d['gap_m'], d['law'],
            d['lat_m'], d['v_law'], d['v_w'], d['w'], d['on'], d['merged'], d['loop'], m.get('w_m'), m.get('w_ok'), m.get('funnel'), d.get('pt'), d.get('own')))
        last, lt = key, t
    m = None
