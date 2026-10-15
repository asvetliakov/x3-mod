# Run177 copy of run176-triage/health176.py with the Run177 windows.
# Run176 Q5 (copy of run175-triage/health175.py with the Run176 windows): health and timing from the extracted rows (ship_steering_summary maxima and last counters, the lane summary's last
# counters, session_end, SETA share and the game/real step ratio per window from ship_trace t_ms and frame_end qpc).
# Usage: python3 -I health175.py ROWS
import sys, re, bisect, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
mx = C.Counter(); last = {}; lane = {}; q = {}; gt = {}; seta = {}; sess = ''
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('frame_end '):
        m = re.search(r' frame=(\d+).*qpc=(\d+)', l)
        if m: q[int(m[1])] = int(m[2])
    elif l.startswith('engine_stage '):
        m = re.search(r' frame=(\d+).*?seta=(\d)', l)
        if m: seta[int(m[1])] = m[2]
    elif l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+)', l); gt.setdefault(int(m[1]), int(m[2]))
    elif l.startswith('ship_steering_summary '):
        d = kv(l)
        for k in ('handler_us_p99', 'handler_us_max', 'ctl_frame_us_max', 'ctl_decide_us_max', 'ctl_live_us_max', 'ctl_sb_us_max', 'ctl_arrival_us_max', 'dock_e_us_max'):
            mx[k] = max(mx[k], float(d.get(k, 0)))
        mx['ctl_budget_cut_max'] = max(mx['ctl_budget_cut_max'], int(d.get('ctl_budget_cut', 0)))
        mx['ctl_members_max'] = max(mx['ctl_members_max'], int(d.get('ctl_members', 0)))
        last = d
    elif l.startswith('ship_steering_lane_summary '): lane = kv(l)
    elif l.startswith('session_end'): sess = l.strip()
for k in sorted(mx): print('max', k, mx[k])
for k in ('rows_dropped', 'ship_trace_dropped', 'ctl_evicted', 'ctl_budget_cut', 'ctl_x_refused', 'ctl_x_wait_s', 'stuck_total', 'emergency', 'e_pass_ctl_why'):
    print('last summary', k, last.get(k))
print('last lane summary', ' '.join('%s=%s' % (k, v) for k, v in lane.items() if k.startswith(('ctl_', 'e_lane'))))
print(sess)
gk = sorted(gt); q0 = q[min(q)]
def G(f): i = bisect.bisect_left(gk, f); return gt[gk[min(i, len(gk) - 1)]]
def win(a, b, name):
    s = C.Counter(v for f, v in seta.items() if a <= f < b); rr = (q[b] - q[a]) / 1e7; gg = (G(b) - G(a)) / 1e3
    print('%-44s frames %5d-%5d real %6.1f s game %7.1f s game/real %5.2f seta share %.3f' % (name, a, b, rr, gg, gg / rr if rr else 0, s['1'] / max(1, sum(s.values()))))
win(min(gt), max(gt), 'all traced frames')
win(884, 25000, 'frames 884..25000 (game 22669..23991)')
win(39748, 46000, 'frames 39748..46000 (game 25046..25256)')
win(46000, 96000, 'frames 46000..96000 (game 25256..27588)')
# the largest game step between consecutive traced frames and its frame
st = sorted((gt[gk[i + 1]] - gt[gk[i]], gk[i + 1]) for i in range(len(gk) - 1) if gk[i + 1] == gk[i] + 1)
print('largest game step between consecutive traced frames (ms, frame):', st[-5:])
print('seta frames', C.Counter(seta.values()))
