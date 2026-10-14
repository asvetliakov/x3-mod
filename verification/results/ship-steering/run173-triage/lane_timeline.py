# Run173 (lane stage 1) per-ship timeline from ship_gate_ctl_member + ship_gate_ctl_lane (same frame, same ship).
# Usage: grep -a -E 'ship_gate_ctl_(member|lane)|^frame_end' x3m.log | python3 -I lane_timeline.py GATE [period_s]
# Prints a row when law/w/order/role/stage/rank/on/merged changes, else every period_s game seconds.
import sys, re, math
gate = sys.argv[1]; per = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
q = {}; mem = {}; last = {}; lastt = {}; t0 = {}
rows = []
for l in sys.stdin:
    if l.startswith('frame_end'):
        d = kv(l)
        if 'qpc' in d: q[int(d['frame'])] = int(d['qpc'])
        continue
    d = kv(l)
    if d.get('gate') != gate: continue
    if l.startswith('ship_gate_ctl_member'): mem[(d['frame'], d['ship'])] = d; continue
    rows.append((d, mem.get((d['frame'], d['ship']), {})))
q0 = q[min(q)]
for d, m in rows:
    s = d['ship']; f = int(d['frame']); t = int(d['t_ms']) / 1000
    key = (d['law'], d['w'], m.get('order'), m.get('role'), m.get('stage'), d['rank'], d['on'], d['merged'])
    if key != last.get(s) or t - lastt.get(s, -1e9) >= per:
        t0.setdefault(s, t)
        p = m.get('p_m', '?').split(',')
        print('%s f=%d real=%.1f g=%.1f gs=%+.1f role=%s st=%s ord=%s law=%s w=%s rank=%s on=%s mg=%s s=%s lat=%s gap=%s v_law=%s v_w=%s v=%s nose=%.1f p=%s'
              % (s[-4:], f, (q.get(f, q0) - q0) / 1e7, t, t - t0[s], m.get('role'), m.get('stage'), m.get('order'), d['law'], d['w'], d['rank'], d['on'],
                 d['merged'], d['lane_s'], d['lat_m'], d['gap_m'], d['v_law'], d['v_w'], m.get('v_ms'), math.degrees(math.acos(max(-1,min(1,float(m.get('nose_f','1')))))), m.get('p_m')))
        last[s] = key; lastt[s] = t
