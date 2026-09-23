"""Run 72 A: frame timing/draw windows, burst frames, collide and memo metrics. Usage: timing_summary.py LOG F1,F2"""
import sys, statistics as st
log, bursts = sys.argv[1], [int(x) for x in sys.argv[2].split(',')]
def kv(line): return dict(t.split('=',1) for t in line.split()[1:] if '=' in t)
ft, cc, cm, fe = [], [], [], {}
want = set(f for b in bursts for f in range(b, b+8))
for line in open(log, errors='replace'):
    h = line[:22]
    if h.startswith('frame_timing '): ft.append(kv(line))
    elif h.startswith('collide_census '): cc.append(kv(line))
    elif h.startswith('collide_memo device'): cm.append(kv(line))
    elif h.startswith('frame_end '):
        d = kv(line); f = int(d['frame'])
        if f in want: fe[f] = (d['dt_ms'], d['draws'])
flight = [d for d in ft if int(d['draws_p50']) > 100]
med = lambda k, rows: st.median(int(d[k]) for d in rows)
print('frame_timing windows', len(ft), 'flight(draws_p50>100)', len(flight))
print('median dt_p50_us', med('dt_p50_us', flight), 'dt_p95_us', med('dt_p95_us', flight), 'draws_p50', med('draws_p50', flight))
for b in bursts:
    w = [d for d in ft if int(d['frame']) - int(d['frames']) < b <= int(d['frame'])]
    for d in w: print('window', d['frame'], 'dt_p50', d['dt_p50_us'], 'dt_p95', d['dt_p95_us'], 'draws_p50', d['draws_p50'])
    print('burst', b, [fe.get(f) for f in range(b, b+8)])
print('collide p1_pairs_sum', sum(int(d['p1_pairs_sum']) for d in cc), 'p1_rejected_sum', sum(int(d['p1_rejected_sum']) for d in cc), 'p2_cands_sum', sum(int(d['p2_cands_sum']) for d in cc))
q = sum(int(d['queries']) for d in cm); h = sum(int(d['hits']) for d in cm)
print('memo windows', len(cm), 'queries', q, 'hits', h, 'hit_rate %.3f' % (h/q if q else 0), 'verify_mismatches', sum(int(d['verify_mismatches']) for d in cm), 'stuck_busy', sum(int(d['stuck_busy']) for d in cm))
