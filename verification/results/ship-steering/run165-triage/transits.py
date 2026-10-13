# Run164 Q1: per member the transit timeline from the ship_gate_ctl rows (first entry as committed/escort, first hold, last release,
# handover (X), dock (order 5), launch), game and real s, commit-to-launch; per gate the span (first entry to last launch) and the
# peak number of members (committed or escort, not evicted) at that gate at once. X nose from crowd.txt. Usage: python3 -I transits.py rows.txt
import sys, os, re, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); ev = C.defaultdict(list); ids = {}; cls = {}
D = os.path.dirname(os.path.abspath(__file__))
for l in open(os.path.join(D, 'caps.txt')): cls[l.split()[0]] = l.split()[1]
nose = {}
for l in open(os.path.join(D, 'crowd.txt')):
    p = l.split('|')
    if len(p) > 4 and p[0].split()[0].startswith('0x') and p[3].strip() != 'no crossing': nose[p[0].split()[0]] = p[3].split()[3]
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids[d['ship']] = d['id']; ev[d['ship']].append((int(d['frame']), d['gate'], d['to'], d['stage'].split('>')[1], d['why']))
T = lambda f: '%.1f/%.1f' % (tm.game(f) / 1000, tm.real(f) / 1000) if f else '-'
G = C.defaultdict(list); occ = C.defaultdict(list)
print('id ship class gate | entry(role) | hold | release | X(handover) | order5 | launch | commit->launch game/real | X nose deg | evictions')
for s in sorted(ev, key=lambda s: ev[s][0][0]):
    r = ev[s]; ent = next((x for x in r if x[4] in ('commit', 'escort')), None)
    if not ent: continue
    gate = r[-1][1]; fe = ent[0]
    first = lambda w: next((x[0] for x in r if x[4] == w), None)
    last = lambda w: next((x[0] for x in reversed(r) if x[4] == w), None)
    fh = next((x[0] for x in r if x[3] == 'hold'), None); fr = last('release'); fx = first('handover'); fd = first('dock'); fl = first('launch')
    cl = '%.1f/%.1f' % ((tm.game(fl) - tm.game(fe)) / 1000, (tm.real(fl) - tm.real(fe)) / 1000) if fl else 'open'
    print('%s %s %s %s | %s(%s) | %s | %s | %s | %s | %s | %s | %s | %d' % (ids[s], s, cls.get(s, '?'), gate[-4:], T(fe), ent[2], T(fh), T(fr), T(fx), T(fd), T(fl), cl,
          nose.get(s, '-'), sum(1 for x in r if x[4] == 'evict')))
    G[gate].append((fe, fl))
    st = None
    for x in r:
        on = x[1] == gate and x[2] in ('committed', 'escort') and x[3] != 'launch'
        if on and st is None: st = x[0]
        elif not on and st is not None: occ[gate].append((st, x[0])); st = None
    if st is not None: occ[gate].append((st, max(tm.rf)))
for g, v in G.items():
    e = sorted([(a, 1) for a, b in occ[g]] + [(b, -1) for a, b in occ[g]], key=lambda x: (x[0], x[1])); n = pk = 0; pf = 0
    for f, dl in e:
        n += dl
        if n > pk: pk, pf = n, f
    fe = min(a for a, b in v); fl = max((b for a, b in v if b), default=None)
    print('gate %s members %d | span first entry %s -> last launch %s = %.1f game s %.1f real s | peak members at once %d at f %d' % (g, len(v), T(fe), T(fl),
          (tm.game(fl) - tm.game(fe)) / 1000, (tm.real(fl) - tm.real(fe)) / 1000, pk, pf))
