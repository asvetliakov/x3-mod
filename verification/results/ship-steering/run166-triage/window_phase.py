# Run166 Q3: the release-window phase per release. For every release (ship_gate_ctl why=release) the head's petal position from its
# ship_gate_ctl_member rows (1 Hz; p_m = (R, U, F) of x - G): b = sin(phi) p_R - cos(phi) p_U (phi = -120 + 40 i deg, i = petal % 7),
# a = p_F; rho = the inner straight's b (min b), a1 = max a - R_h (the front turn's foremost point), a0 = a1 - S_h; the arc s (0 at the
# inner straight's back end, ship_gate_ctl_core.h petal_project) and the window arc (W: the back turn's last 60 deg + the inner straight,
# s in [Lap - pi R/3, Lap) U [0, S_h]: z <= a1 and nose.F >= 0.5, core.h:2444). Events: commit (role committed), first funnel-free row
# (ship_gate_ctl_seq busy_by 0 with this cand), the release. Then the window wait (funnel free -> release) and three estimates:
#  (a) a second window half a lap on (figure-8 / two-sided): the wait to the nearer window start at the free moment's s;
#  (b) speed shaping from the commit (t_free known: free_ms): the earliest t >= t_free reachable in a window at v in [25, 60] m/s from
#      the commit's s (the holder may park at 25 m/s inside W);
#  (a+b) both; (c) a smaller petal R_h 500 m (S_h 1500): the out-of-window arc 4118 m instead of 6736 m, the wait scaled by that ratio.
# Usage: python3 -I window_phase.py rows.txt
import sys, os, re, math, bisect, statistics as ST, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
R, S = 1000.0, 1500.0; HALF = math.pi * R; LAP = 2 * S + 2 * HALF; WB = HALF / 3  # the back turn's last 60 deg
tm = sb_time.load(sys.argv[1]); ids = {}; ev = []; mem = C.defaultdict(list); seq = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); ev.append(d)
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; mem[d['ship']].append(d)
    elif l.startswith('ship_gate_ctl_seq '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); seq[d['gate']].append(d)
def proj(a, b, a0, a1, rho):
    bc = rho + R
    if a0 <= a <= a1: return a - a0 if b <= bc else S + HALF + (a1 - a)
    if a > a1: return S + R * max(0.0, math.atan2(a - a1, bc - b))
    return 2 * S + HALF + R * max(0.0, math.atan2(a0 - a, b - bc))
inW = lambda s: s % LAP >= LAP - WB or s % LAP <= S
def to_win(s, shift=0.0):  # arc from s to the start of the window (0 inside); shift: a second window that far on
    s = (s - shift) % LAP
    return 0.0 if (s >= LAP - WB or s <= S) else LAP - WB - s
G = lambda d: int(d['t_ms']) / 1000; RL = lambda f: tm.real(int(f)) / 1000
tot = C.Counter()
print('Lap %.0f m, window %.0f m (%.0f%%); out-of-window arc %.0f m; at 60 m/s lap %.0f s, at 40 %.0f s, at 25 %.0f s' % (LAP, WB + S, 100 * (WB + S) / LAP, LAP - WB - S, LAP / 60, LAP / 40, LAP / 25))
for r in [d for d in ev if d['why'] == 'release']:
    s_, g, t_rel = r['ship'], r['gate'], int(r['t_ms'])
    com = next((int(d['t_ms']) for d in ev if d['ship'] == s_ and d['to'] == 'committed' and d['why'] in ('commit',) and int(d['t_ms']) <= t_rel and t_rel - int(d['t_ms']) < 2000000), None)
    com = max((int(d['t_ms']) for d in ev if d['ship'] == s_ and d['why'] == 'commit' and int(d['t_ms']) <= t_rel), default=None)
    rows = [d for d in mem[s_] if d['stage'] == 'hold' and d['gate'] == g and int(d['t_ms']) <= t_rel and int(d['petal']) >= 0]
    sq = [d for d in seq[g] if d['cand'] == s_ and d['busy_by'] == '0x00000000' and com is not None and com <= int(d['t_ms']) <= t_rel]
    f0 = int(sq[0]['t_ms']) if sq else None
    wr = [d for d in sq if d['window'] == '0']; wait_free = len(wr)  # 1 Hz rows: game s with funnel free, head out of window
    line = 'gate %s release %-5s game %8.1f real %5.1f | commit %s' % (g[-4:], ids[s_], t_rel / 1000, RL(r['frame']), '%.1f' % (com / 1000) if com else '-')
    if not rows or f0 is None:
        print(line + ' | no hold rows / never free while head: released on commit, wait 0'); continue
    pet = int(rows[-1]['petal']); phi = math.radians(-120 + 40 * (pet % 7)); sp, cp = math.sin(phi), math.cos(phi)
    seg = [d for d in rows if int(d['petal']) == pet]
    A = [d['p'][2] for d in seg]; B = [sp * d['p'][0] - cp * d['p'][1] for d in seg]
    a1 = max(A) - R; a0 = a1 - S; rho = min(B)
    sof = lambda d: proj(d['p'][2], sp * d['p'][0] - cp * d['p'][1], a0, a1, rho)
    at = lambda t: min(seg, key=lambda d: abs(int(d['t_ms']) - t))
    dc, dfr, dr = at(com), at(f0), at(t_rel)
    v = [float(d['v_ms']) for d in seg if f0 <= int(d['t_ms']) <= t_rel]; vc = [float(d['v_ms']) for d in seg if com <= int(d['t_ms']) <= f0]
    vm = ST.median(v) if v else 60.0
    wait = (t_rel - f0) / 1000; sfr = sof(dfr); d_free = to_win(sfr)
    # (a) a second window half a lap on
    da = min(to_win(sfr), to_win(sfr, LAP / 2)); wa = da / vm
    # (b) from the commit's s and time, the free time known: earliest t >= t_free (from commit) with a reachable point in W
    T0 = (f0 - com) / 1000; sc0 = sof(dc); wb = None; t = T0
    while t < T0 + 400:
        lo, hi = sc0 + 25 * t, sc0 + 60 * t; x = lo; ok = False
        while x <= hi:
            if inW(x): ok = True; break
            x += 25
        if ok: wb = t - T0; break
        t += 0.5
    # (a+b) both: the speed shaping toward either window
    wab = None; t = T0
    while t < T0 + 400:
        x = sc0 + 25 * t; ok = False
        while x <= sc0 + 60 * t:
            if inW(x) or inW(x - LAP / 2): ok = True; break
            x += 25
        if ok: wab = t - T0; break
        t += 0.5
    tot['ab'] += wait - (wab if wab is not None else wait)
    # (c) R_h 500: the wait scaled by the out-of-window arc ratio
    wc = wait * (2 * S + 2 * math.pi * 500 - (math.pi * 500 / 3 + S) - 0) / (LAP - WB - S) if d_free > 0 else 0.0
    tot['wait'] += wait; tot['a'] += wait - wa; tot['b'] += wait - (wb if wb is not None else wait); tot['c'] += wait - wc
    rr = (RL(r['frame']) - tm.real(tm.frame_at_game(f0)) / 1000)
    print(line + ' | petal %d rho %.0f a1 %.0f | s@commit %.0f (%s) v %.0f..%.0f | first free %.1f (free_ms %s) s %.0f (%s) | release s %.0f nose %.2f | '
          'wait free->release %.1f game s %.1f real s, funnel-free out-of-window rows %d, arc to window at free %.0f m, v med %.0f | '
          'lap %.0f s | (a) %.1f s saves %.1f | (b) %s s saves %s | (a+b) %s s | (c) %.1f s saves %.1f' % (
          pet, rho, a1, sc0, 'W' if inW(sc0) else 'out', min(vc or [0]), max(vc or [0]), f0 / 1000, sq[0]['free_ms'], sfr, 'W' if inW(sfr) else 'out',
          sof(dr), float(dr['nose_f']), wait, rr, wait_free, d_free, vm, LAP / vm, wa, wait - wa, '%.1f' % wb if wb is not None else '>400',
          '%.1f' % (wait - wb) if wb is not None else '-', '%.1f' % wab if wab is not None else '>400', wc, wait - wc))
print('totals (game s): window wait %.1f | saved (a) %.1f (b) %.1f (a+b) %.1f (c) %.1f' % (tot['wait'], tot['a'], tot['b'], tot['ab'], tot['c']))
