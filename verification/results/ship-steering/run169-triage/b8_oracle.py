# Run169 Q1 (B.8 oracle): for every escort/leader-launch pair (escort_wait.py's pairing), the escort's member row nearest the leader's
# launch (first row at or after it, and the last row before it): stage, ringed, d_join, t_rel, rho, zx, s, stage_s, v, distance to the gate;
# then order 4 (+game s after the launch) and the share of the escort's member rows launch->order 4 written ~60 m/s.
# Usage: python3 -I b8_oracle.py rows.txt
import sys, os, re, math, collections as C
D = os.path.dirname(os.path.abspath(__file__))
GT = {l.split()[0]: [float(x) for x in l.split()[1].split(',')] for l in open(os.path.join(D, 'gates.txt')) if l.startswith('0x') and ',' in l.split()[1]}
ids = {}; ev = C.defaultdict(list); mem = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); ev[d['ship']].append(d)
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; mem[d['ship']].append(d)
launch = {s: [int(d['t_ms']) for d in v if d['stage'].endswith('>launch') and not d['stage'].startswith('launch')] for s, v in ev.items()}
commit = {s: [int(d['t_ms']) for d in v if d['why'] == 'commit'] for s, v in ev.items()}
pairs = []
for s_ in mem:
    lead = None; seen = set()
    for d in mem[s_]:
        if d['role'] == 'escort' and d['target'] not in GT and d['target'] != '0x00000000': lead = d['target']
        if lead and d['role'] == 'escort':
            for lt in launch.get(lead, []):
                if lt <= int(d['t_ms']) and (lead, lt) not in seen and int(d['t_ms']) - lt < 30000: seen.add((lead, lt)); pairs.append((lt, s_, lead, d['gate']))
print('escort leader gate | +s of row | stage ringed | d_join m | t_rel ms | rho zx s stage_s | v_ms | d_G km | order4 +s | rows to order4: n, v>=55 share, d_join first/last')
for lt, s_, L, g in sorted(pairs):
    G = GT[g]; rows = mem[s_]
    aft = next((d for d in rows if int(d['t_ms']) >= lt), None)
    c4 = next((c for c in commit.get(s_, []) if c >= lt - 1000), None)
    if not aft: print(ids.get(s_, s_), ids.get(L, L), g[-4:], '| no row'); continue
    dg = math.hypot(*aft['p']) / 1000  # p_m is gate-relative
    seg = [d for d in rows if lt <= int(d['t_ms']) <= (c4 if c4 else lt + 60000)]
    v55 = sum(float(d['v_ms']) >= 55 for d in seg) / max(1, len(seg))
    print(ids.get(s_, s_), ids.get(L, L), g[-4:], '| +%.1f' % ((int(aft['t_ms']) - lt) / 1000), '|', aft['stage'], aft.get('ringed'), '|', aft.get('d_join'), '|', aft.get('t_rel'),
          '|', aft.get('rho'), aft.get('zx'), aft.get('s'), aft.get('stage_s'), '|', aft['v_ms'], '| %.1f' % dg, '|', '%.1f' % ((c4 - lt) / 1000) if c4 else '-',
          '| %d %.2f %s/%s' % (len(seg), v55, seg[0].get('d_join') if seg else '-', seg[-1].get('d_join') if seg else '-'))
