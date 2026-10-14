# Run167 Q8: for every escort whose leader launched, the wait from the leader's launch (ship_gate_ctl stage ->launch) to the escort's own
# order 4 (ship_gate_ctl why=commit, else the first order-4 trace row), and what bound it. From the escort's ship_trace rows (every frame):
# every order change (the script's passes: order 0x0 gap -> 0x14/0x15 re-issue) with game offset and distance to the escort's gate G
# (trace pos / 500 = m); whether any pass came with the escort inside 10 km; the member rows' stage/v in the interval (hold rows, path
# length on the petal = laps of 9.28 km, v phases 60 / 25 / other); then commit -> release -> X -> launch and the hold after the commit.
# Usage: python3 -I escort_wait.py rows.txt
import sys, os, re, math, collections as C
D = os.path.dirname(os.path.abspath(__file__)); LAP = 2 * 1500 + 2 * math.pi * 1000
GT = {l.split()[0]: [float(x) for x in l.split()[1].split(',')] for l in open(os.path.join(D, 'gates.txt')) if l.startswith('0x') and ',' in l.split()[1]}
ids = {}; ev = C.defaultdict(list); mem = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); ev[d['ship']].append(d)
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; mem[d['ship']].append(d)
esc = {s for s, v in mem.items() if any(d['role'] == 'escort' for d in v)}
launch = {s: [int(d['t_ms']) for d in v if d['stage'].endswith('>launch') and not d['stage'].startswith('launch')] for s, v in ev.items()}
tr = C.defaultdict(list)  # escort -> [(t, order, pos m)]
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=\d+ t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+) tgt=(\S+) \S+ pos=(\S+) fwd=(\S+)', l)
        if m and m[2] in esc: tr[m[2]].append((int(m[1]), m[3], m[4], [float(x) / 500 for x in m[5].split(',')], [float(x) for x in m[6].split(',')]))
pairs = []
for s_ in esc:
    lead = None; seen = set()
    for d in mem[s_]:
        if d['role'] == 'escort' and d['target'] not in GT and d['target'] != '0x00000000': lead = d['target']
        if lead and d['role'] == 'escort':
            for lt in launch.get(lead, []):
                if lt <= int(d['t_ms']) and (lead, lt) not in seen and int(d['t_ms']) - lt < 30000: seen.add((lead, lt)); pairs.append((lt, s_, lead, d['gate']))
print('escort leader gate | leader launch game | escort order 4 +game s (why) | d_G at launch km | passes (re-issues after a 0x0 gap) before order 4: '
      'count, +s/d_km each, first inside 10 km | min d_G km | member stages (rows) | hold path km (laps), nose turn launch->order 4 (trace) | v rows 60/25/other | '
      'commit->release->X->launch game s | hold rows after commit, path km, nose turn commit->release (trace)')
for lt, s_, L, g in sorted(pairs):
    G = GT[g]; dist = lambda p: math.dist(p, G) / 1000
    E = ev[s_]; cm = [int(d['t_ms']) for d in E if d['why'] == 'commit' and int(d['t_ms']) >= lt and d['gate'] == g]
    # the escort may leave this leader (why=leader/target) before any order 4
    lv = [int(d['t_ms']) for d in E if int(d['t_ms']) > lt and d['why'] in ('leader', 'target') and d['gate'] == g]
    cm = [t for t in cm if not lv or t < min(lv)]
    o4 = [t for t, o, tg, p, *_ in tr[s_] if t >= lt and o == '0x4' and (not lv or t < min(lv))]
    tc = min(cm) if cm else (min(o4) if o4 else None)
    end = tc if tc else (min(lv) if lv else max(t for t, *_ in tr[s_]))
    T = [x for x in tr[s_] if lt <= x[0] <= end]
    d0 = dist(T[0][3]) if T else float('nan')
    passes = []; prev = T[0][1] if T else None
    for t, o, tg, p, *_ in T[1:]:
        if o != prev and prev == '0x0' and o != '0x0': passes.append(((t - lt) / 1000, dist(p), o))
        prev = o
    f10 = next(('+%.0f' % a for a, b, o in passes if b <= 10.0), 'none')
    M = [d for d in mem[s_] if lt <= int(d['t_ms']) <= end and d['gate'] == g]
    st = C.Counter(d['stage'] for d in M); H = [d for d in M if d['stage'] == 'hold']
    path = sum(math.dist(a['p'], b['p']) for a, b in zip(H, H[1:])) / 1000
    vv = C.Counter('60' if float(d['v_ms']) >= 59 else '25' if float(d['v_ms']) <= 26 else 'other' for d in M)
    def turn(X):  # accumulated nose turn (deg) over trace rows sampled >= 1 game s apart (per-frame fwd jitter would accumulate)
        Y = []
        for x in X:
            if not Y or x[0] - Y[-1][0] >= 1000: Y.append(x)
        X = Y; a = 0.0
        for u, v in zip(X, X[1:]):
            c = max(-1.0, min(1.0, sum(x * y for x, y in zip(u[4], v[4])))); a += math.degrees(math.acos(c))
        return a
    tw = turn(T)
    out = ''
    if cm:
        tl = lambda w: next((int(d['t_ms']) for d in E if d['why'] == w and int(d['t_ms']) >= tc and d['gate'] == g), None)
        rl, xo, la = tl('release'), tl('handover'), next((x for x in launch.get(s_, []) if x >= tc), None)
        f = lambda x: '%.1f' % ((x - tc) / 1000) if x else '-'
        HC = [d for d in mem[s_] if d['stage'] == 'hold' and d['gate'] == g and tc <= int(d['t_ms']) <= (rl or tc)]
        out = '%s / %s / %s | %d, %.1f km, turn %.0f deg' % (f(rl), f(xo), f(la), len(HC), sum(math.dist(a['p'], b['p']) for a, b in zip(HC, HC[1:])) / 1000,
              turn([x for x in tr[s_] if tc <= x[0] <= (rl or tc)]))
    why = 'commit' if cm else ('order-4 row' if tc else ('left leader +%.0f' % ((min(lv) - lt) / 1000) if lv else 'open at log end'))
    print('%-6s %-6s %s | %.1f | %s (%s) | %.1f | %d: %s | first <=10 km %s | %.1f | %s | %.1f (%.2f), turn %.0f deg | %s | %s' % (
        ids.get(s_), ids.get(L), g[-4:], lt / 1000, '%.1f' % ((tc - lt) / 1000) if tc else '-', why, d0, len(passes),
        ' '.join('+%.0f/%.1f' % (a, b) for a, b, o in passes), f10, min(dist(x[3]) for x in T) if T else float('nan'),
        dict(st), path, path * 1000 / LAP, tw, dict(vv), out))
