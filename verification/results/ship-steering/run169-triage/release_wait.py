# Run167 Q2: each release's wait from the funnel-free moment. Per release (ship_gate_ctl why=release): the gate's last seq row with
# busy_by != 0 before it (t_busy_end; the free moment = the next seq row), the head's commit, w = release - max(free, commit, hold entry); the
# head's seq rows in [max(free, commit), release] with window=0 (head out of window) and timing=0; the feasibility interval 1 s + the
# largest game step in that span (frame_end / ship_gate_ctl_member t_ms deltas). Exception class (B.6 oracle): committed inside W
# within |W|/25 = 143.8 s before the free moment. Usage: python3 -I release_wait.py rows.txt
import sys, re, collections as C, bisect
ids = {}; ev = []; seq = C.defaultdict(list); T = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); ev.append(d)
    elif l.startswith('ship_gate_ctl_seq '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); seq[d['gate']].append(d)
    elif l.startswith('ship_gate_ctl_member '):
        T.append(int(re.search(r' t_ms=(\d+)', l)[1]))
T = sorted(set(T))
print('gate id | commit | hold entry | free moment (last busy by) | release | wait from free game s | out-of-window rows, timing=0 rows | max step ms | feasibility s | <= interval')
for r in [d for d in ev if d['why'] == 'release']:
    s, g, t = r['ship'], r['gate'], int(r['t_ms'])
    com = max(int(d['t_ms']) for d in ev if d['ship'] == s and d['why'] == 'commit' and int(d['t_ms']) <= t)
    S = [d for d in seq[g] if int(d['t_ms']) <= t]
    busy = [i for i, d in enumerate(S) if d['busy_by'] != '0x00000000']
    if busy and busy[-1] + 1 < len(S): fr, by = int(S[busy[-1] + 1]['t_ms']), ids.get(S[busy[-1]]['busy_by'], S[busy[-1]]['busy_by'])
    elif busy: fr, by = t, ids.get(S[busy[-1]]['busy_by'])
    else: fr, by = 0, '-'
    he = max((int(d['t_ms']) for d in ev if d['ship'] == s and d['gate'] == g and d['stage'].endswith('>hold') and int(d['t_ms']) <= t), default=0)
    t0 = max(fr, com, he); w = (t - t0) / 1000
    rows = [d for d in S if int(d['t_ms']) >= t0 and d['cand'] == s]
    ow = sum(d['window'] == '0' for d in rows); tz = sum(d['timing'] == '0' for d in rows)
    i0, i1 = bisect.bisect_left(T, t0), bisect.bisect_right(T, t)
    st = max((b - a for a, b in zip(T[i0:i1], T[i0 + 1:i1])), default=0)
    print('%s %-6s | %.1f | %.1f | %.1f (%s) | %.1f | %.1f | %d, %d | %d | %.1f | %s' % (g[-4:], ids[s], com / 1000, he / 1000, fr / 1000, by, t / 1000, w, ow, tz, st, 1 + st / 1000,
          'yes' if w <= 1 + st / 1000 else 'no'))
