"""Run160b Q1: the exit-gate crowd. Per capital: class, r (E r_B), arrival gate (first rows' gate warn / d to gate), first order-4
sample toward the exit gate after its escort/arrival legs (cap_orders sources), crossing (ship_gate_approach cross_to_launch_ms=0
row) and launch, game and real s, cross heading err / v. Crowd: peak number of capitals between order-4 start (the last order-4
run that ends at the crossing) and launch at once, and the most crossings inside any 120 game s.
Usage: python3 -I crowd.py rows.txt"""
import sys, re, bisect, collections as C
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
tm = {}; rl = {}; S = C.defaultdict(dict); ap = C.defaultdict(list); rB = {}; arr = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+)', l); f = int(m[1]); tm.setdefault(f, int(m[2]))
        if m[3] in caps: S[m[3]][f] = m[4]
    elif l.startswith('frame_end '):
        m = re.search(r'frame=(\d+) .*elapsed_ms=(\d+)', l); rl[int(m[1])] = int(m[2])
    elif l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        if d['A'] in caps:
            S[d['A']].setdefault(f, d['A_order'])
            if d['B_class'] == '18' and d['A'] not in arr: arr[d['A']] = (d['B'], f, d['d_m'])
        if d['B'] in caps: S[d['B']].setdefault(f, d['B_order']); rB[d['B']] = d['r_B']
    elif l.startswith(('ship_avoid_engine_warn ', 'ship_avoid_engine_warn_pre ')):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['obj'] in caps: S[d['obj']].setdefault(int(d['frame']), d['order'])
    elif l.startswith('ship_gate_approach ') and ' capital=1 ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ap[d['obj']].append(d)
fs = sorted(tm)
def g(f): i = min(bisect.bisect_left(fs, f), len(fs) - 1); return tm[fs[i]] / 1e3
def r(f): return rl.get(f, rl.get(f + 1, 0)) / 1e3
W = []
print('obj class r_m | first row: gate d_m frame | order-4 run start game/real | cross frame game real hdg_err cross_v | launch game real cross_to_launch_s')
for o in sorted(caps, key=lambda o: min(S[o])):
    a = ap.get(o); seq = sorted(S[o].items())
    cr = next((int(x['frame']) for x in (a or []) if x['cross_to_launch_ms'] == '0'), None)
    la = next((int(x['frame']) for x in (a or []) if x['cross_to_launch_ms'] != '0'), None)
    end = cr or seq[-1][0]
    # start of the final order-4 commitment: the earliest 0x4 sample after the last 0x14/0x15/0x1e sample before 'end'
    lastesc = max([f for f, od in seq if f < end and od in ('0x14', '0x15', '0x1e', '0x21')] or [seq[0][0]])
    st = next((f for f, od in seq if lastesc <= f <= end and od == '0x4'), None)
    W.append((o, g(st) if st else None, g(la) if la else g(seq[-1][0]), g(cr) if cr else None))
    x = next((x for x in (a or []) if x['cross_to_launch_ms'] != '0'), None)
    print('%s %s %s | %s | %s | %s | %s' % (o, caps[o], rB.get(o, '-'), arr.get(o, '-'), '%.1f/%.1f' % (g(st), r(st)) if st else '-',
          '%d %.1f %.1f %s %s' % (cr, g(cr), r(cr), x['cross_heading_err_deg'], x['cross_v_ms']) if cr and x else 'no crossing',
          '%.1f %.1f %.1f' % (g(la), r(la), int(x['cross_to_launch_ms']) / 1e3) if la and x else '-'))
ev = sorted([(t0, 1) for o, t0, t1, c in W if t0] + [(t1, -1) for o, t0, t1, c in W if t0])
n = pk = 0; pt = None
for t, dlt in ev:
    n += dlt
    if n > pk: pk, pt = n, t
crs = sorted(c for o, t0, t1, c in W if c)
best = max(((sum(1 for y in crs if x <= y <= x + 120), x) for x in crs), default=(0, 0))
print('peak committed (order-4 start..launch) at once: %d at game %.1f; most crossings in 120 s: %d from game %.1f' % (pk, pt, best[0], best[1]))
gap = [round(b - a, 1) for a, b in zip(crs, crs[1:])]; print('crossing gaps (game s):', gap)
