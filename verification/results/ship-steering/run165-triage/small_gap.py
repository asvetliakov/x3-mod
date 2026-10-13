# Run165 Q3: e_small_gap (B.4: site E's capital small-ship rule fired on an A whose decision is older than the fresh window, i.e. the
# script's order-0 gap). The counter's per-interval increments from ship_steering_summary, and the traced ship_avoid_e_verdict rows
# with verdict=suppress_small and A_age_ms > fresh window (250 ms, or 200 + 2 step) grouped by A (pointer, its trace class), A_order,
# B_code, B_order and B's leader == A (A's own escort). Usage: python3 -I small_gap.py rows.txt
import sys, re, collections as C
cls = {}; last = None; inc = []; prev = None; G = C.Counter(); A = C.Counter(); ages = []; tot = 0
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=\d+ t_ms=\d+ obj=(\S+) class=(\S+)', l); cls[m[1]] = m[2]
    elif l.startswith('ship_steering_summary '):
        f = re.search(r' frame=(\d+)', l); m = re.search(r' e_small_gap=(\d+)', l); q = re.search(r' e_suppressed_small=(\d+)', l)
        if m:
            v = int(m[1]); last = (int(f[1]), v, int(q[1]) if q else -1)
            if prev is not None and v > prev: inc.append((int(f[1]), v - prev))
            prev = v
    elif l.startswith('ship_avoid_e_verdict ') and 'verdict=suppress_small' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); tot += 1; a = int(d['A_age_ms'])
        if a > 250:
            ages.append(a); G[(cls.get(d['A'], '?'), d['A_order'], d['B_code'], d['B_order'], 'own escort' if d['B_leader'] == d['A'] else 'other')] += 1
            A[(d['A'], cls.get(d['A'], '?'))] += 1
print('last summary (frame, e_small_gap, e_suppressed_small):', last)
print('counter increments: %d intervals, sum %d, largest %s' % (len(inc), sum(x[1] for x in inc), sorted(inc, key=lambda x: -x[1])[:5]))
print('traced suppress_small rows %d, with A_age_ms > 250: %d (A_age median %s, max %s)' % (tot, len(ages), sorted(ages)[len(ages) // 2] if ages else '-', max(ages) if ages else '-'))
for k, n in G.most_common(15): print('%6d A %s A_order %s | B %s B_order %s %s' % (n, *k))
print('by A:', A.most_common(10))
