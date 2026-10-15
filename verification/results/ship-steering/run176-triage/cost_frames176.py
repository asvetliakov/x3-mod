# Run176 cost triage Q1/Q3: the context of every ship_steering_handler_slow row and of the summary window holding the controller-frame max.
# For each slow row: the event frame (the ship_trace frame whose t_ms equals the row's t_ms; the row itself is drained at the next boundary),
# frame_end dt_ms and the qpc interval to the previous slow row (real s) and game s, and every non-per-frame row carrying frame=F-2..F+1
# (high-rate ship rows counted, not printed). Usage: python3 -I cost_frames176.py ROWS (rows = log minus row types with >= 40000 rows,
# frame_end/ship_trace/engine_stage kept; see cost_rows.sh)
import sys, re, collections as C
NOISY = ('ship_trace', 'frame_end', 'engine_stage', 'ship_avoid_e_verdict', 'ship_avoid_detour', 'ship_gate_ctl_member', 'ship_gate_ctl_lane',
         'ship_avoid_dock', 'telemetry_metric', 'ship_gate_ctl_pair', 'ship_steering_decision', 'ship_gate_ring_node',
         'volumetric_fog_prefill', 'emission_source_gain_refused_state', 'telemetry_summary', 'engine_memory')
t2f = {}; fe = {}; slow = []; byf = C.defaultdict(list); summ = []
for l in open(sys.argv[1], errors='replace'):
    k = l.split(' ', 1)[0]
    m = re.search(r' frame=(\d+)', l)
    f = int(m[1]) if m else None
    if k == 'ship_trace' and f is not None:
        t = re.search(r' t_ms=(\d+)', l)
        if t: t2f.setdefault(int(t[1]), f)
    elif k == 'frame_end':
        fe[f] = (int(re.search(r'dt_ms=(\d+)', l)[1]), int(re.search(r'qpc=(\d+)', l)[1]))
    elif k == 'ship_steering_handler_slow':
        slow.append(l.strip())
    elif k == 'ship_steering_summary':
        d = dict(re.findall(r'(\w+)=(\S+)', l)); summ.append((f, d))
    if f is not None:
        byf[f].append(k if k in NOISY else l.strip()[:260])
def ctx(F0, F1, label):
    print('---', label)
    for f in range(F0, F1 + 1):
        rows = byf.get(f, []); n = C.Counter(r for r in rows if r in NOISY)
        print('  frame %d dt_ms=%s noisy=%s' % (f, fe.get(f, ('?',))[0], dict(n)))
        for r in rows:
            if r not in NOISY and not r.startswith('frame_end'): print('    ', r)
prev = None
for s in slow:
    d = dict(re.findall(r'(\w+)=(\S+)', s)); t = int(d['t_ms']); fd = int(d['frame']); fev = t2f.get(t)
    q = fe.get(fev or fd, (0, 0))[1]
    dq = (q - prev[0]) / 1e7 if prev else 0; dg = (t - prev[1]) / 1e3 if prev else 0
    print('SLOW event_frame=%s drain_frame=%d t_ms=%d us=%s cycles=%s obj=%s order=%s snap_walk=%s | since previous: real %.1f s game %.1f s'
          % (fev, fd, t, d['us'], d['cycles'], d['obj'], d['order'], d['snap_walk'], dq, dg))
    prev = (q, t)
    ctx((fev or fd) - 2, fd + 1, 'context')
# the controller frame max and the budget cut: the summary windows
# (ctl_*_us_max and ctl_budget_cut are session values: a row when they change; handler_us_max is per window: a row above 500 us)
pf = None; pc = 0; pm = 0.0
for f, d in summ:
    c = int(d.get('ctl_budget_cut', 0)); fm = float(d.get('ctl_frame_us_max', 0)); hm = float(d.get('handler_us_max', 0))
    if c != pc or fm != pm or hm > 500:
        print('SUMMARY frame=%d prev_summary=%s interval_ms=%s handler_us_p99=%s handler_us_max=%s ctl_frame_us_max=%s ctl_decide_us_max=%s '
              'ctl_frame_decisions_max=%s ctl_budget_cut=%s ctl_prof_us_max=%s ctl_members=%s'
              % (f, pf, d.get('interval_ms'), d.get('handler_us_p99'), d.get('handler_us_max'), d.get('ctl_frame_us_max'),
                 d.get('ctl_decide_us_max'), d.get('ctl_frame_decisions_max'), c, d.get('ctl_prof_us_max'), d.get('ctl_members')))
    pf = f; pc = c; pm = fm
