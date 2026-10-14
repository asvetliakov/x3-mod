# Run167 Q2: a committed head's 1 Hz track from its commit to its release: game s, arc s on the fitted petal, z - a1 (a1 = the fitted
# front end), nose.F, v_ms, and the gate's seq row (busy_by id, window, timing, free_ms). Usage: python3 -I head_track.py rows.txt ship [every]
import sys, re, math, collections as C
R, S = 1000.0, 1500.0; HALF = math.pi * R; LAP = 2 * S + 2 * HALF
sh = sys.argv[2]; ev = []; mem = []; seq = {}; ids = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
        if d['ship'] == sh: ev.append(d)
    elif l.startswith('ship_gate_ctl_member ') and 'ship=' + sh in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; mem.append(d)
    elif l.startswith('ship_gate_ctl_seq '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['cand'] == sh: seq[int(d['t_ms']) // 1000] = d
com = max(int(d['t_ms']) for d in ev if d['why'] == 'commit'); rel = max(int(d['t_ms']) for d in ev if d['why'] == 'release')
rows = [d for d in mem if com <= int(d['t_ms']) <= rel and d['stage'] == 'hold']
pet = int(rows[-1]['petal']); phi = math.radians(-120 + 40 * (pet % 7)); sp, cp = math.sin(phi), math.cos(phi)
seg = [d for d in mem if d['stage'] == 'hold' and int(d['petal']) == pet and d['gate'] == rows[-1]['gate']]
a1 = max(d['p'][2] for d in seg) - R; a0 = a1 - S; rho = min(sp * d['p'][0] - cp * d['p'][1] for d in seg); bc = rho + R
def proj(a, b):
    if a0 <= a <= a1: return a - a0 if b <= bc else S + HALF + (a1 - a)
    if a > a1: return S + R * max(0.0, math.atan2(a - a1, bc - b))
    return 2 * S + HALF + R * max(0.0, math.atan2(a0 - a, b - bc))
ev_ = int(sys.argv[3]) if len(sys.argv) > 3 else 1
print('ship %s id %s commit %.1f release %.1f petal %d a1 %.0f (fit) | W = s >= %.0f or s <= %.0f' % (sh, ids[sh], com / 1000, rel / 1000, pet, a1, LAP - HALF / 3, S + HALF / 3))
for i, d in enumerate(rows):
    if i % ev_: continue
    t = int(d['t_ms']); a, b = d['p'][2], sp * d['p'][0] - cp * d['p'][1]; q = seq.get(t // 1000, {})
    print('%.1f s %5.0f z-a1 %6.0f nose %.2f v %4.1f | busy %s window %s timing %s free_ms %s' % (t / 1000, proj(a, b), a - a1, float(d['nose_f']), float(d['v_ms']),
          ids.get(q.get('busy_by'), q.get('busy_by', '-')), q.get('window', '-'), q.get('timing', '-'), q.get('free_ms', '-')))
