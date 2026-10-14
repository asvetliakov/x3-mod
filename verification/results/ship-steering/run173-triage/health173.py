# Run173 health/timing numbers for the convoy at gate 0x932c7070 (leader 0x92e0aec8, escorts 0x92e11db0 / 0x932c35f0).
# Usage: python3 -I health173.py x3m.log   (streams the log once; prints only counts/maxima)
import sys, re, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
SHIPS = {'0x92e0aec8': 'L', '0x92e11db0': 'E1', '0x932c35f0': 'E2'}
q = {}; seta = {}; gt = {}; mx = C.Counter(); last = {}; why = C.Counter(); warn = C.Counter(); dts = []
ev = {}  # (frame -> game ms) from ship_trace for real/game mapping
for l in open(sys.argv[1], 'rb'):
    l = l.decode('latin-1')
    if l.startswith('frame_end '):
        m = re.search(r' frame=(\d+).*qpc=(\d+)', l)
        if m: q[int(m[1])] = int(m[2])
    elif l.startswith('engine_stage '):
        m = re.search(r' frame=(\d+).*?seta=(\d)', l)
        if m: seta[int(m[1])] = m[2]
    elif l.startswith('ship_trace '):
        m = re.search(r' frame=(\d+) t_ms=(\d+).*? dt_ms=(\d+)', l)
        if m: gt.setdefault(int(m[1]), int(m[2])); dts.append((int(m[1]), int(m[3])))
    elif l.startswith('ship_steering_summary '):
        d = kv(l)
        for k in ('handler_us_p99', 'handler_us_max', 'ctl_frame_us_max', 'ctl_decide_us_max', 'ctl_live_us_max', 'join_eval_us_max', 'ctl_sb_us_max'):
            mx[k] = max(mx[k], float(d.get(k, 0)))
        last = d
    elif l.startswith('ship_gate_ctl '):
        d = kv(l)
        if d['gate'] == '0x932c7070': why[(SHIPS.get(d['ship'], 'other'), d['why'])] += 1
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l); warn[(SHIPS.get(d['obj'], 'other'), d['class'])] += 1
q0 = q[min(q)]
import bisect
_gk = sorted(gt)
def G(f):
    i = bisect.bisect_left(_gk, f); return gt[_gk[min(i, len(_gk) - 1)]]
for k in sorted(mx): print('max', k, mx[k])
for k in ('ctl_live', 'ctl_evicted', 'ctl_budget_cut', 'ctl_go_around', 'ctl_x_refused', 'ctl_x_wait_s', 'ctl_releases', 'ctl_yields',
          'ctl_unresolved', 'rows_dropped', 'ship_trace_dropped', 'member_leader_gone', 'handled_by_order_total', 'e_pass_ctl_why', 'gate_capital_not_kept_nose'):
    print('last', k, last.get(k))
print('ctl why', dict(why))
print('engine 0x12 warn rows (ship, obstacle class)', dict(warn))
def win(a, b, name):
    s = C.Counter(seta[f] for f in seta if a <= f < b)
    st = [dt for f, dt in dts if a <= f < b]
    print('%s frames %d-%d real %.1f-%.1f s (%.1f) game %.1f-%.1f s (%.1f) seta share %.3f max step %d ms' % (name, a, b, (q[a] - q0) / 1e7,
          (q[b] - q0) / 1e7, (q[b] - q[a]) / 1e7, G(a) / 1e3, G(b) / 1e3, (G(b) - G(a)) / 1e3, s['1'] / max(1, sum(s.values())), max(st)))
win(718, 24763, 'all-convoy')
win(10000, 23704, '15 km (L ~f10000) .. last launch (E1 f23704)')
win(12431, 14104, 'L handover .. L order 8')
for f in (718, 10000, 11696, 12052, 12431, 12515, 14104, 15269, 16543, 21536, 23686):
    print('event f=%d real=%.2f game=%.3f' % (f, (q[f] - q0) / 1e7, G(f) / 1e3))
