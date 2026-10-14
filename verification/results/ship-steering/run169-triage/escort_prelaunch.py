# Run168 Q3: escort member rows (role escort, stage hold) before the leader's launch: |p| band (km) and v_ms histogram per escort, and
# the script's re-targets (ship_gate_ctl why=leader/target rows of escorts). Usage: python3 -I escort_prelaunch.py rows.txt
import sys, os, re, math, collections as C
GATES = {l.split()[0] for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gates.txt')) if l.startswith('0x')}
ids = {}; lau = C.defaultdict(list); mem = C.defaultdict(list); rt = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
        if d['stage'].endswith('>launch') and not d['stage'].startswith('launch'): lau[d['ship']].append(int(d['t_ms']))
        if d['why'] in ('leader', 'target'): rt.append(d)
    elif l.startswith('ship_gate_ctl_member ') and ' role=escort ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; mem[d['ship']].append(d)
tot_v = C.Counter(); tot_d = C.Counter()
for s_, rows in mem.items():
    lead = None; P = C.defaultdict(list)
    for d in rows:
        if d['target'] not in GATES and d['target'] != '0x00000000': lead = d['target']
        if d['stage'] != 'hold' or lead is None: continue
        t = int(d['t_ms']); L = [x for x in lau.get(lead, []) if x >= t - 1000]
        if L and t < min(L): P[(lead, d['gate'])].append(d)
    for (L, g), H in P.items():
        dv = C.Counter('<=26' if float(d['v_ms']) <= 26 else '26-39' if float(d['v_ms']) < 39 else '39-41' if float(d['v_ms']) <= 41 else '41-59' if float(d['v_ms']) < 59 else '>=59' for d in H)
        dd = C.Counter('%d-%d' % (int(math.dist(d['p'], (0, 0, 0)) / 1000), int(math.dist(d['p'], (0, 0, 0)) / 1000) + 1) for d in H)
        tot_v.update(dv); tot_d.update(dd)
        path = sum(math.dist(a['p'], b['p']) for a, b in zip(H, H[1:])) / 1000
        print('%-6s leader %-6s %s | hold rows before launch %d (game %.1f..%.1f) | path %.1f km = %.2f laps | v %s | |p| km %s' % (ids.get(s_, s_), ids.get(L, L), g[-4:], len(H),
              int(H[0]['t_ms']) / 1000, int(H[-1]['t_ms']) / 1000, path, path / 9.283, dict(sorted(dv.items())), dict(sorted(dd.items()))))
print('total v', dict(sorted(tot_v.items())), '| total |p| km', dict(sorted(tot_d.items())))
print('== script re-targets (why=leader/target)')
for d in rt: print('f %s game %.1f gate %s id %s %s>%s why %s stage %s' % (d['frame'], int(d['t_ms']) / 1000, d['gate'][-4:], d['id'], d['from'], d['to'], d['why'], d['stage']))
