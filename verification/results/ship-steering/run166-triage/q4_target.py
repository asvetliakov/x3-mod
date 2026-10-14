# Run166 Q4 (coordinator's hypothesis): the lap-speed target t_next vs the funnel's real free moment, for the three held releases.
# (4a) per head: commit (why=commit), seq free_ms at the commit (t_free = commit + free_ms), t_next = t_free - (kLeadM 2500 + kPetalRh 1000
#      + rho) / 50 (ship_gate_ctl_core.h:2538; rho from window_phase.py's fit), the head's v (member rows) commit -> first free, first
#      free (seq busy_by 0 with this cand) and free_ms there.
# (4b) a simulated head on the measured petal arc: from the commit's s, it reaches the window at the earliest in-window moment at or
#      after a target (constant speed in [25, 60] m/s), dwells at 25 m/s inside the window, laps at 60 outside; released at the first
#      1 Hz seq second with the funnel free (busy_by 0; free from the last free row on) while in the window. Targets: P = the predicted
#      funnel-free moment from the commit's free_ms (t_free - 2 s gap - the predecessor's measured order-5 leg X->launch - 2,500 m / its
#      measured final speed), and I = the measured first free (an ideal prediction). Window W = [Lap - 1047, Lap) U [0, 1500].
# (4c) as 4b with the window through the front turn's first 60 deg: [Lap - 1047, Lap) U [0, 2547] (3,594 m).
# Usage: python3 -I q4_target.py rows.txt
import sys, os, re, math, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
R, S = 1000.0, 1500.0; HALF = math.pi * R; LAP = 2 * S + 2 * HALF; WB = HALF / 3
tm = sb_time.load(sys.argv[1])
H = {'0x8e5a1bb0': ('25763', 4160.0, 1081.0, '0x4bc686b0'), '0xa7574688': ('5748', 3435.0, 345.0, '0xa75715c8'), '0x1dc1aac8': ('4059', 3377.0, 105.0, '0x8e5b5030')}
ev = C.defaultdict(list); seq = C.defaultdict(list); mem = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ev[d['ship']].append((int(d['t_ms']), d['why'], int(d['frame'])))
    elif l.startswith('ship_gate_ctl_seq '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['cand'] in H: seq[d['cand']].append((int(d['t_ms']), d['busy_by'] == '0x00000000', int(d['free_ms'])))
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['ship'] in H or any(d['ship'] == h[3] for h in H.values()): mem[d['ship']].append((int(d['t_ms']), float(d['v_ms']), d['stage']))
def sim(s0, T, free, ext, t_end=600.0):
    win = lambda x: (x % LAP) >= LAP - WB or (x % LAP) <= S + (WB if ext else 0.0)
    # phase 1: arrival point at T: the first in-window point reachable in [s0 + 25 T, s0 + 60 T], else the next window start at 60 m/s
    lo, hi = s0 + 25 * T, s0 + 60 * T; x = lo; xa = None
    while x <= hi:
        if win(x): xa = x; break
        x += 5
    if xa is None:  # beyond reach at T: 60 m/s to the next window start
        x = hi
        while not win(x): x += 5
        ta = (x - s0) / 60; xa = x
    else: ta = T
    t, pos = 0.0, s0; v1 = (xa - s0) / ta if ta > 0 else 60
    while t < t_end:
        if t < ta: pos = s0 + v1 * t
        if t >= ta: pos += (25 if win(pos) else 60) * 0.5
        if win(pos) and free(t): return t
        t += 0.5
    return None
for sh, (nm, rho, s0, pred) in H.items():
    com = max(t for t, w, f in ev[sh] if w == 'commit' and t <= max(t for t, w2, f2 in ev[sh] if w2 == 'release'))
    rel = max(t for t, w, f in ev[sh] if w == 'release')
    sq = [x for x in seq[sh] if com - 1500 <= x[0] <= rel]
    fm0 = next(x[2] for x in sq if x[0] >= com - 1500); t_free = com + fm0
    t_next = t_free - (2500 + 1000 + rho) / 50 * 1000
    ff = next(x for x in sq if x[1] and x[0] >= com)
    vs = [v for t, v, st in mem[sh] if com <= t <= ff[0]]
    px = next(t for t, w, f in ev[pred] if w == 'handover'); pl = next(t for t, w, f in ev[pred] if w == 'launch')
    vf = max(v for t, v, st in mem[pred] if st == 'funnel' and px - 30000 <= t <= px)
    P = t_free - 2000 - (pl - px) - 2500 / vf * 1000
    fr = [((x[0] - com) / 1000, x[1]) for x in sq]; last_free = max(t for t, b in fr if not b) if any(not b for t, b in fr) else -1
    def free(t):
        if t > last_free: return True
        k = min(fr, key=lambda z: abs(z[0] - t)); return k[1]
    print('%s commit %.1f (real %.1f) | free_ms@commit %.1f -> t_free %.1f | t_next %.1f = commit %+.1f s | v commit->free %.0f..%.0f | first free %.1f '
          '(commit %+.1f s), free_ms there %.1f; first free -> t_free(commit) %.1f s | actual release %.1f (wait from first free %.1f)' % (
          nm, com / 1000, tm.real(next(f for t, w, f in ev[sh] if t == com)) / 1000, fm0 / 1000, t_free / 1000, t_next / 1000, (t_next - com) / 1000, min(vs), max(vs),
          ff[0] / 1000, (ff[0] - com) / 1000, ff[2] / 1000, (t_free - ff[0]) / 1000, rel / 1000, (rel - ff[0]) / 1000))
    T_ff = (ff[0] - com) / 1000
    for lab, tgt in (('P (pred: t_free - 2 - leg5 %.1f - 2500/%.0f)' % ((pl - px) / 1000, vf), P), ('I (first free)', ff[0])):
        T = max(0.0, (tgt - com) / 1000); out = []
        for ext in (False, True):
            r = sim(s0, T, free, ext); out.append('%s: release commit+%s wait from first free %s' % ('4c W+60deg' if ext else '4b W', '%.1f' % r if r is not None else '-',
                                                     '%.1f' % (r - T_ff) if r is not None else '-'))
        print('   target %s = commit %+.1f s | %s' % (lab, (tgt - com) / 1000, ' | '.join(out)))
    # check: the simulator with today's policy (60 m/s from the commit) reproduces the measured release
    r = sim(s0, 0.0, free, False) if False else None
