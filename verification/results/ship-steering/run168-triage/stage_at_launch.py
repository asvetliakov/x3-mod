# Run168 Q1 (B.7 escort staging): for every escort whose leader was released, the escort's petal arc position s (lap_speed.py's fit
# and projection), its B.7 stage s (escort_stage_s, closed form on the fitted a0/a1/rho), |p| to G and v_ms at the leader's release,
# every 20 game s after it, the leader's launch and launch + 4.5 s; and over [release, launch + 4.5 s] the share of rows inside the
# escort's B.7 window [stage, kWinCloseS] and the v histogram. --design: a1 from zx_estimate(r_gate 1104.5, the ship's r_m from
# ship_gate_launch) instead of the fit (the fit needs the escort to reach the front turn's apex; rho stays the fit's min b).
# Usage: python3 -I stage_at_launch.py rows.txt [--design]
import sys, os, re, math, collections as C
R, S = 1000.0, 1500.0; HALF = math.pi * R; LAP = 2 * S + 2 * HALF; CLOSE = S + HALF / 3; D = 9500.0
GATES = {l.split()[0] for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gates.txt')) if l.startswith('0x')}
ids = {}; ev = C.defaultdict(list); mem = C.defaultdict(list); RS = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_launch ') and ' capital=1 ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); RS[d['ship']] = float(d['r_m'])
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); ev[d['ship']].append(d)
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; d['t'] = int(d['t_ms']); mem[d['ship']].append(d)
def proj(a, b, a0, a1, rho):
    bc = rho + R
    if a0 <= a <= a1: return a - a0 if b <= bc else S + HALF + (a1 - a)
    if a > a1: return S + R * max(0.0, math.atan2(a - a1, bc - b))
    return 2 * S + HALF + R * max(0.0, math.atan2(a0 - a, b - bc))
def stage(a0, a1, rho):
    bc = rho + R; dc = math.hypot(a1, bc) - R
    if dc * dc > 0.9e8: return -1.0
    if rho * rho <= D * D:
        h = math.sqrt(D * D - rho * rho); lo, hi = max(a0, -h), min(a1, h)
        if lo <= hi: return lo - a0
    c = math.hypot(a1, bc); q = (c * c + R * R - D * D) / (2 * R * c)
    if q > 1: return -1.0
    q = max(q, -1.0); phi = math.atan2(-a1, bc); al = math.atan2(math.sqrt(1 - q * q), q); t = max(0.0, phi - al)
    if t > phi + al or t > math.pi: return -1.0
    s = S + R * t
    return s if s < CLOSE else -1.0
def tr(rows):
    for d in rows:
        if 'launch' in d['stage'] or d['stage'].endswith('>launch'): pass
    return rows
rel = {}; lau = {}
for s_, E in ev.items():
    for d in E:
        if d['stage'].startswith('hold>funnel'): rel.setdefault((s_, d['gate']), d['t'] if 't' in d else int(d['t_ms']))
        if d['stage'].endswith('>launch') and not d['stage'].startswith('launch'): lau.setdefault((s_, d['gate']), int(d['t_ms']))
print('escort leader gate | petal | fit a0 a1 rho m | stage s (|p| at stage km) | samples (+s from leader release: s / |p| km / v / in-window / off-track km) | in-window rows [release, launch+4.5] | v hist')
done = set()
for s_, rows in sorted(mem.items(), key=lambda kv: ids.get(kv[0], '')):
    lead = None
    for d in rows:
        if d['role'] == 'escort' and d['target'] not in GATES and d['target'] != '0x00000000': lead = d['target']
        d['lead'] = lead
    for (L, g), tr_ in rel.items():
        if (s_, L, g) in done or not any(d['lead'] == L and d['gate'] == g and d['role'] == 'escort' for d in rows): continue
        tl = lau.get((L, g)); 
        if tl is None: continue
        H = [d for d in rows if d['gate'] == g and d['stage'] == 'hold' and int(d['petal']) >= 0]
        W = [d for d in rows if d['gate'] == g and tr_ <= d['t'] <= tl + 4500 and d['role'] == 'escort']
        if not H or not W: continue
        done.add((s_, L, g)); pet = int(H[-1]['petal'])
        P = [d for d in H if int(d['petal']) == pet]; ph = math.radians(-120 + 40 * (pet % 7)); sp, cp = math.sin(ph), math.cos(ph)
        A = [d['p'][2] for d in P]; B = [sp * d['p'][0] - cp * d['p'][1] for d in P]
        a1 = max(A) - R; a0 = a1 - S; rho = min(B)
        if '--design' in sys.argv and s_ in RS:  # the controller's own geometry: a1 = zx_estimate(r_gate, r) - kLeadM - k kRingStepM - R_h
            a1 = -(2 * (1104.5 + RS[s_]) + 0.75 * 1104.5) - 2500 - (pet // 7) * 7800 - R; a0 = a1 - S
        st = stage(a0, a1, rho)
        def at(d):
            a = d['p'][2]; b = sp * d['p'][0] - cp * d['p'][1]; s = proj(a, b, a0, a1, rho)
            iw = (st >= 0 and st <= s <= CLOSE) or (st < 0 and (s >= LAP - HALF / 3 or s <= CLOSE))
            c = cp * d['p'][0] + sp * d['p'][1]; bc = rho + R  # off-track: in-plane distance to the racetrack plus the out-of-plane part
            if a0 <= a <= a1: q = min(abs(b - rho), abs(b - rho - 2 * R))
            else: q = abs(math.hypot(a - (a1 if a > a1 else a0), b - bc) - R)
            return s, math.dist(d['p'], (0, 0, 0)) / 1000, float(d['v_ms']), iw, d['stage'], math.hypot(q, c) / 1000
        samp = []
        for k in list(range(0, int((tl - tr_) / 1000) + 1, 20)) + ['L', 'L+4.5']:
            t = tr_ + 1000 * k if isinstance(k, int) else tl + (0 if k == 'L' else 4500)
            d = min(W, key=lambda x: abs(x['t'] - t))
            s, p, v, iw, sg, off = at(d); samp.append('%s: %.0f/%.1f/%.0f/%s/off %.1f%s' % (k, s, p, v, 'W' if iw else '-', off, '' if sg == 'hold' else '(' + sg + ')'))
        X = [at(d) for d in W if d['stage'] == 'hold']; n = sum(1 for x in X if x[3])
        vh = C.Counter('60' if x[2] >= 59 else '25' if x[2] <= 26 else '40' if 39 <= x[2] <= 41 else 'other' for x in X)
        sa = st if st >= 0 else None
        if sa is not None:
            pa = (a0 + sa, rho) if sa <= S else (a1 + R * math.sin((sa - S) / R), rho + R - R * math.cos((sa - S) / R))
        print('%-6s %-6s %s | %d | %.0f %.0f %.0f | %s | %s | %d/%d | %s' % (ids.get(s_, s_), ids.get(L, L), g[-4:], pet, a0, a1, rho,
              '%.0f (%.2f)' % (sa, math.hypot(*pa) / 1000) if sa is not None else 'none (far petal)', ' '.join(samp), n, len(X), dict(vh)))
