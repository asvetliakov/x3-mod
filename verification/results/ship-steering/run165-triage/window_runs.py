# Run165 Q2: runs of ship_gate_ctl_seq rows with the funnel free, a committed head and window=0 (head out of its release window),
# longest first, with game/real span and the head's distance to X. Usage: python3 -I window_runs.py rows.txt
import sys, os, re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); ids = {}; runs = []; cur = None
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
    if not l.startswith('ship_gate_ctl_seq '): continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); hit = d['busy_by'] == '0x00000000' and d['role'] == 'committed' and d['window'] == '0'
    if hit and cur and cur['cand'] == d['cand'] and int(d['t_ms']) - cur['t1'] <= 1500: cur['t1'] = int(d['t_ms']); cur['f1'] = int(d['frame']); cur['dx'].append(float(d['dx_m']))
    elif hit: cur = dict(cand=d['cand'], gate=d['gate'][-4:], t0=int(d['t_ms']), t1=int(d['t_ms']), f0=int(d['frame']), f1=int(d['frame']), dx=[float(d['dx_m'])]); runs.append(cur)
    else: cur = None
for r in sorted(runs, key=lambda r: r['t0'] - r['t1']):
    print('gate %s head %s f %d-%d game %.1f +%.1f real %.1f +%.1f dx %.0f..%.0f' % (r['gate'], ids.get(r['cand']), r['f0'], r['f1'], r['t0'] / 1000, (r['t1'] - r['t0']) / 1000 + 1,
          tm.real(r['f0']) / 1000, (tm.real(r['f1']) - tm.real(r['f0'])) / 1000 + 0.1, min(r['dx']), max(r['dx'])))
