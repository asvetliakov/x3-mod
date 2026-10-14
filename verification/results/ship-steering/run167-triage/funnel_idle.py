# Run165 Q2: the funnel's idle time per gate: ship_gate_ctl_seq rows (1 per game s while holders exist) with busy_by none and at
# least one holder (stage hold; inbound excluded), split by why no release: no committed candidate (escorts only, waiting for the
# script's order 4), head out of window, head timing, slot block, other. Game s (row count) and real s. Usage: python3 -I funnel_idle.py rows.txt
import sys, os, re, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); T = C.defaultdict(lambda: [0, 0.0]); B = C.Counter()
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_gate_ctl_seq '): continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); g = d['gate'][-4:]; hold = int(d['holders']) - int(d['inbound'])
    f = int(d['frame']); r = (tm.real(tm.frame_at_game(int(d['t_ms']) + 1000)) - tm.real(f)) / 1000
    if d['busy_by'] != '0x00000000': B[g] += 1; continue
    if hold <= 0: continue
    if d['cand'] == '0x00000000' or d['role'] != 'committed': k = 'no committed candidate (role %s)' % d['role']
    elif float(d['block_ms']) > 0: k = 'slot block'
    elif d['window'] != '1': k = 'head out of window'
    elif d['timing'] != '1': k = 'head timing'
    else: k = 'free, window+timing ok (release that second)'
    T[(g, k)][0] += 1; T[(g, k)][1] += r
print('busy rows per gate:', dict(B))
for (g, k), (n, r) in sorted(T.items()): print('gate %s %-50s %5d game s %6.1f real s' % (g, k, n, r))
