# Run167 Q3 (B.6 lap law): every holder's 1 Hz ship_gate_ctl_member rows (stage hold) classified by phase, v_ms histogram per phase.
# Phases: head (role committed): in W (s in [Lap-1047, Lap) U [0, 1500+1047] and z <= a1 + 866, nose.F >= 0.5) / out of W;
# escort before its leader's launch (leader = the member row's target; launch = the leader's ship_gate_ctl stage ->launch);
# escort after the launch still inbound on the 0x15 approach (leader = the escort's last ship target), on its petal before its first row with |p| <= 10 km (the 0x15 approach) / after that pass (B.6: must be <= 25 m/s).
# Per (escort, leader): launch, order 4 (commit) +game s, mean v after launch, min |p| after launch, petal/ring, laps above 25 m/s.
# Usage: python3 -I lap_speed.py rows.txt
import sys, os, re, math, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
R, S = 1000.0, 1500.0; HALF = math.pi * R; LAP = 2 * S + 2 * HALF; WT = HALF / 3; FRONT = R * math.sin(math.pi / 3)
GATES = {l.split()[0] for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gates.txt')) if l.startswith('0x')}
tm = sb_time.load(sys.argv[1]); ids = {}; launch = C.defaultdict(list); commit = C.defaultdict(list); mem = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
        if d['stage'].endswith('>launch') and not d['stage'].startswith('launch'): launch[d['ship']].append(int(d['t_ms']))
        if d['why'] == 'commit': commit[d['ship']].append(int(d['t_ms']))
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; mem[d['ship']].append(d)
def proj(a, b, a0, a1, rho):
    bc = rho + R
    if a0 <= a <= a1: return a - a0 if b <= bc else S + HALF + (a1 - a)
    if a > a1: return S + R * max(0.0, math.atan2(a - a1, bc - b))
    return 2 * S + HALF + R * max(0.0, math.atan2(a0 - a, b - bc))
def geom(rows):  # per (gate, petal) hold rows -> (sp, cp, a0, a1, rho)
    pet = int(rows[0]['petal']); phi = math.radians(-120 + 40 * (pet % 7)); sp, cp = math.sin(phi), math.cos(phi)
    A = [d['p'][2] for d in rows]; B = [sp * d['p'][0] - cp * d['p'][1] for d in rows]
    return sp, cp, max(A) - R - S, max(A) - R, min(B)
def bin_(v): return '<=26' if v <= 26 else '26-39' if v < 39 else '39-41' if v <= 41 else '41-59' if v < 59 else '>=59'
H = C.defaultdict(C.Counter); HS = C.defaultdict(C.Counter); per = []
for s_, rows in mem.items():
    hold = [d for d in rows if d['stage'] == 'hold' and int(d['petal']) >= 0]
    if not hold: continue
    lead = None
    for d in rows:  # the leader: the escort's last ship target (after the launch the target becomes the gate, order 0x15)
        if d['role'] == 'escort' and d['target'] not in GATES and d['target'] != '0x00000000': lead = d['target']
        d['lead'] = lead
    hold = [d for d in rows if (d['stage'] == 'hold' and int(d['petal']) >= 0) or (d['role'] == 'escort' and d['stage'] == 'inbound' and int(d['petal']) >= 0)]
    grp = C.defaultdict(list)
    for d in hold: grp[(d['gate'], d['petal'])].append(d)
    G = {k: geom(v) for k, v in grp.items()}; G0 = {}
    ep = C.defaultdict(lambda: {'n': 0, 'v': [], 'minp': 9e9, 'ph': C.Counter(), 'over25_s': 0, 'pet': None, 'first_in10': None})
    for d in hold:
        t = int(d['t_ms']); v = float(d['v_ms'])
        if (d['gate'], d['petal']) not in G and d['stage'] == 'inbound': G[(d['gate'], d['petal'])] = (0, 0, 0, 0, 0)
        sp, cp, a0, a1, rho = G[(d['gate'], d['petal'])]
        a, b = d['p'][2], sp * d['p'][0] - cp * d['p'][1]; s = proj(a, b, a0, a1, rho)
        if d['gate'] + d['petal'] not in G0: G0[d['gate'] + d['petal']] = 1
        if d['stage'] == 'inbound' and d['role'] == 'escort':
            L = d['lead']; lt = [x for x in launch.get(L, []) if x <= t]
            if not lt: continue
            e = ep[(L, max(lt))]; e['pet'] = d['petal']; dist = math.sqrt(sum(x * x for x in d['p'])); e['minp'] = min(e['minp'], dist)
            if e['first_in10'] is None and dist <= 10000: e['first_in10'] = t
            ph = 'escort after launch, inbound (0x15 approach)'; e['ph'][ph] += 1; e['n'] += 1; e['v'].append(v); H[ph][bin_(v)] += 1; continue
        if d['role'] == 'committed':
            inW = (s >= LAP - WT or s <= S + WT) and a <= a1 + FRONT and float(d['nose_f']) >= 0.5
            ph = 'head in W' if inW else 'head out of W'
        elif d['role'] == 'escort':
            L = d['lead']; lt = [x for x in launch.get(L, []) if x <= t]
            if not lt: ph = 'escort, leader not launched'
            else:
                k = (L, max(lt)); e = ep[k]; e['pet'] = d['petal']; dist = math.sqrt(sum(x * x for x in d['p']))
                if e['first_in10'] is None and dist <= 10000: e['first_in10'] = t
                ph = 'escort after launch, after 10 km pass' if e['first_in10'] is not None else 'escort after launch, before 10 km pass'
                e['n'] += 1; e['v'].append(v); e['minp'] = min(e['minp'], dist); e['ph'][ph] += 1
                if e['first_in10'] is not None and v > 26: e['over25_s'] += 1
        else: ph = 'role ' + d['role']
        H[ph][bin_(v)] += 1; HS[(ids.get(s_, s_), d['gate'][-4:], ph)][bin_(v)] += 1
    for (L, lt), e in ep.items():
        c = [x for x in commit[s_] if x >= lt]; tc = min(c) if c else None
        per.append((ids.get(s_, s_), ids.get(L, L), lt / 1000, (tc - lt) / 1000 if tc else None, e))
print('== v_ms histogram per phase (1 Hz member rows, stage hold; game s)')
for ph in sorted(H): print('%-42s n %5d  %s' % (ph, sum(H[ph].values()), dict(sorted(H[ph].items()))))
print('== escort per leader launch: launch game | order 4 +game s | hold rows after launch | mean v | first 10 km pass +s | rows >26 m/s after the pass | min |p| m | petal ring')
for e_id, l_id, lt, dc, e in sorted(per, key=lambda x: x[2]):
    f10 = '%.0f' % ((e['first_in10'] / 1000) - lt) if e['first_in10'] else 'never'
    print('%-6s leader %-6s | %.1f | %s | %d | %.1f | %s | %d | %.0f | %s ring %d' % (e_id, l_id, lt, '%.1f' % dc if dc is not None else 'open',
          e['n'], sum(e['v']) / len(e['v']), f10, e['over25_s'], e['minp'], e['pet'], int(e['pet']) // 7))
print('== per ship, gate, phase (hold rows; the inbound approach rows are in the totals only)')
for k in sorted(HS, key=lambda k: (k[1], k[0])): print('%-6s %s %-42s n %4d %s' % (k[0], k[1], k[2], sum(HS[k].values()), dict(sorted(HS[k].items()))))
