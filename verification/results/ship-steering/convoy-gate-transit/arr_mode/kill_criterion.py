# The flight's criterion for the arrival side vanilla (convoy-gate-transit.md section 13.0, the orchestrator's decision of 2026-10-16). By
# construction the arrival side is the engine's and the script's: a capital whose order 4 goes to the gate it came through within 900 s + the
# step is never a committed or escort member and no write of ours reaches it. The flight judges two things, both printed per gate here:
#  (1) the departure side unchanged: per departure group (committed members chained within 300 s), the span from the first member within
#      15 km to the last launch, the stops (v < 15 m/s for >= 3 s on member rows, before the launch), the turn-aways (>= 3 s with the nose
#      against F, more than 2.5 km in front of the plane; the member rows' p_m and nose_f are in the gate frame), the lane stop writes and the members' 0x12 episodes; Run176's departures are the baseline (run this
#      on its log). A regression there: the diff is the culprit.
#  (2) the arrival side as the user sees it (vanilla): per return trip (arrival-side ships of a gate chained within 900 s), the arrivals,
#      launches, span, the 0x12 episodes and the plane crossings without a jump (ship_trace rows, when the ship was traced), and the lane
#      writes and board entries on its ships (0 by construction on a build with the vanilla latch).
# Arrival-side ships: ship_gate_ctl_arr rows (a build with the latch), keyed (ship, gate, arrival time): a ship's later return trips through
# the same gate are trips of their own; the row count is checked against the last summary's ctl_arr_mode=. A log without them falls back to the rule on the log: a commit or escort
# entry on gate X within 900 s + 5 s of the ship's own ship_gate_launch row at X. Member 0x12 episode: ship_avoid_engine_warn order=0x12 on
# the ship, a new episode after a gap of more than 3 frames (b_convoy_spans.py's rule).
#
#   python3 -I kill_criterion.py LOG_OR_ROWS      (one pass; about a second on Run176's 805 MB log)
import bisect, collections as C, math, re, sys

ARR_MS, STEP_MS, ARR_CHAIN_MS, DEP_CHAIN_MS = 900000, 5000, 900000, 300000
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
PFX = ('ship_gate_ctl ', 'ship_gate_ctl_member ', 'ship_gate_ctl_lane ', 'ship_gate_ctl_arr ', 'ship_gate_launch ', 'ship_avoid_engine_warn ',
       'ship_steering_lane_summary ', 'ship_trace ')
gates, launch, ctl, mem, lane, warns, arr_rows, trace, fr2t = {}, C.defaultdict(list), C.defaultdict(list), C.defaultdict(list), \
    C.defaultdict(list), C.defaultdict(list), [], C.defaultdict(list), {}
ctl_arr_mode = None  # the last lane summary's ctl_arr_mode= (approaches latched vanilla since install)
with open(sys.argv[1], 'rb') as f:
    for b in f:
        if not b.startswith(b'ship_'):
            continue
        l = b.decode('ascii', 'replace')
        m = re.match(r'ship_\S+ frame=(\d+) t_ms=(\d+)', l)
        if m and int(m[1]) not in fr2t:
            fr2t[int(m[1])] = int(m[2])
        if not l.startswith(PFX):
            continue
        h = l.split(' ', 1)[0]
        if h == 'ship_steering_lane_summary':
            m2 = re.search(r' ctl_arr_mode=(\d+)', l)
            if m2:
                ctl_arr_mode = int(m2[1])
            if 'gates_gate:F:G:r=' in l:
                for e in l.split('gates_gate:F:G:r=')[1].split()[0].split('|'):
                    p = e.split(':')
                    if len(p) == 4 and p[0] not in gates:
                        gates[p[0]] = ([float(x) for x in p[1].split(',')], [float(x) for x in p[2].split(',')], float(p[3]))
            continue
        d = kv(l)
        if h == 'ship_gate_ctl':
            ctl[d['ship']].append((int(d['t_ms']), d['gate'], d['from'], d['to'], d['why'], d['stage']))
        elif h == 'ship_gate_ctl_member':
            mem[d['ship']].append((int(d['t_ms']), d['gate'], [float(x) for x in d['p_m'].split(',')], d['role'], d['stage'],
                                   float(d['nose_f']), float(d['v_ms'])))
        elif h == 'ship_gate_ctl_lane':
            lane[d['ship']].append((int(d['t_ms']), d['gate'], d.get('w', 'none')))
        elif h == 'ship_gate_ctl_arr':
            arr_rows.append((d['ship'], d['gate'], int(d['t_ms'])))
        elif h == 'ship_gate_launch':
            launch[d['ship']].append((int(d['t_ms']), d['gate']))
        elif h == 'ship_avoid_engine_warn' and d.get('order') == '0x12':
            warns[d['obj']].append((int(d['frame']), d['other'], d.get('code', '-')))
        elif h == 'ship_trace' and 'pos' in d and 't_ms' in d:
            trace[d['obj']].append((int(d['t_ms']), [int(x) / 500.0 for x in d['pos'].split(',')]))
fk = sorted(fr2t)
ft = lambda fr: fr2t[fk[max(bisect.bisect_right(fk, fr) - 1, 0)]]
has_latch = bool(arr_rows)


def last_launch_before(s, g, t):
    ts = [lt for lt, lg in launch[s] if lg == g and lt <= t]
    return ts[-1] if ts else None


# arrival-side (ship, gate, arrival time): one per return trip of that ship through that gate (a ship may make several)
arr_keys = set()
if has_latch:
    for s_, g_, t in arr_rows:
        arr_keys.add((s_, g_, last_launch_before(s_, g_, t) or t))
else:
    for s_, rows in ctl.items():
        for t, g_, fr, to, why, st in rows:
            ta = last_launch_before(s_, g_, t)
            if to in ('committed', 'escort') and fr != to and ta is not None and t - ta <= ARR_MS + STEP_MS:
                arr_keys.add((s_, g_, ta))
print('# %s; arrival-side ships %s; %d arrival-side (ship, gate, arrival) keys' % (sys.argv[1].rsplit('/', 1)[-1],
      'from ship_gate_ctl_arr rows (the vanilla latch)' if has_latch else 'inferred (no ship_gate_ctl_arr rows: an entry within 900 s of the own launch)',
      len(arr_keys)))
print('# ship_gate_ctl_arr rows %d, the last summary\'s ctl_arr_mode= %s%s' % (
    len(arr_rows), ctl_arr_mode if ctl_arr_mode is not None else '-',
    '' if ctl_arr_mode is None or ctl_arr_mode == len(arr_rows) else ' (MISMATCH: rows dropped (--debug ring 8, rows_dropped ctl), or the log started after install)'))


def jump(s, g, t0):
    j = [t for t, gg, fr, to, why, st in ctl[s] if gg == g and t >= t0 and to == 'none' and st.startswith('launch')]
    return j[0] if j else None


def episodes(s, t0, t1):
    out, lastf = [], -10
    for fr, o, c in warns[s]:
        t = ft(fr)
        if t0 <= t <= t1:
            if fr - lastf > 3:
                out.append('%.1f:%s(%s)' % (t / 1000, c, o[-4:]))
            lastf = fr
    return out


def runs(rows, min_ms=3000):  # episodes of consecutive rows (t, flag) with the flag set, lasting >= min_ms
    out, start, last = [], None, None
    for t, ok in rows:
        if ok:
            start = start if start is not None else t
            last = t
        else:
            if start is not None and last - start >= min_ms:
                out.append('%.1f+%.1f' % (start / 1000, (last - start) / 1000))
            start = None
    if start is not None and last - start >= min_ms:
        out.append('%.1f+%.1f' % (start / 1000, (last - start) / 1000))
    return out


for g in sorted({gg for rows in ctl.values() for t, gg, *_ in rows} | {gg for s, gg, ta in arr_keys}):
    F, G, r = gates.get(g, (None, None, 1105.0))
    # the arrival-side windows here: (ship) -> [(arrival, jump or None)]
    win = C.defaultdict(list)
    for s, gg, ta in arr_keys:
        if gg == g:
            win[s].append((ta, jump(s, g, ta)))
    in_arr = lambda s, t: any(ta <= t and (tj is None or t <= tj) for ta, tj in win[s])
    # (1) the departure side: committed or escort entries outside an arrival-side window of that ship (first entry per ship and stretch)
    dep = {}
    for s, rows in ctl.items():
        ts = [t for t, gg, fr, to, why, st in rows if gg == g and to in ('committed', 'escort') and not in_arr(s, t)]
        if ts:
            dep[s] = ts[0]
    groups = []
    for s, t in sorted(dep.items(), key=lambda x: x[1]):
        if groups and t - groups[-1][-1][1] <= DEP_CHAIN_MS:
            groups[-1].append((s, t))
        else:
            groups.append([(s, t)])
    for grp in groups:
        lines, t15s, tls, n_stop, n_turn, n_w, n_ep = [], [], [], 0, 0, 0, 0
        for s, t0 in grp:
            tl = jump(s, g, t0)
            m = [x for x in mem[s] if x[1] == g and x[0] >= t0 and (tl is None or x[0] <= tl) and x[3] != 'arrival']
            t15 = next((x[0] for x in m if math.dist(x[2], (0, 0, 0)) <= 15000), None)  # (p_m: the gate frame R, U, F)
            stops = runs([(x[0], x[6] < 15.0) for x in m])
            turns = runs([(x[0], x[2][2] < -2500.0 and x[5] < 0.0) for x in m])
            w = sum(1 for t, gg, wk in lane[s] if gg == g and t >= t0 and (tl is None or t <= tl) and wk == 'stop')
            ep = episodes(s, t0, tl if tl is not None else 1 << 62)
            n_stop += len(stops)
            n_turn += len(turns)
            n_w += w
            n_ep += len(ep)
            if t15 is not None:
                t15s.append(t15)
            if tl is not None:
                tls.append(tl)
            lines.append('     %s 15km %s launch %s | stops %d %s | turn-aways %d %s | lane stop writes %d | 0x12 %d %s' % (
                s, '%.1f' % (t15 / 1000) if t15 else '-', '%.1f' % (tl / 1000) if tl else '-', len(stops), stops[:4], len(turns), turns[:4],
                w, len(ep), ep[:4]))
        span = (max(tls) - min(t15s)) / 1000 if t15s and len(tls) == len(grp) else None
        print('gate %s departure %.1f members %d: span 15 km -> last launch %s, stops %d, turn-aways %d, lane stop writes %d, member 0x12 %d%s' % (
            g, grp[0][1] / 1000, len(grp), '%.1f s' % span if span is not None else 'open (%d of %d launched)' % (len(tls), len(grp)), n_stop, n_turn,
            n_w, n_ep, ' -> CHECK against the baseline' if n_stop or n_turn or n_ep else ''))
        for x in lines:
            print(x)
    # (2) the arrival side (vanilla), per return trip
    trips = []
    for s, gg, ta in sorted(arr_keys, key=lambda x: x[2]):
        if gg != g:
            continue
        if trips and ta - max(x[1] for x in trips[-1]) <= ARR_CHAIN_MS:
            trips[-1].append((s, ta))
        else:
            trips.append([(s, ta)])
    for trip in trips:
        lines, tot_ep, tot_cross, tot_w, tot_entry, ends = [], 0, 0, 0, 0, []
        for s, ta in trip:
            tj = jump(s, g, ta)
            ends.append(tj)
            te = tj if tj is not None else 1 << 62
            ep = episodes(s, ta, te)
            cross, prev = [], None
            if F:
                for t, p in trace[s]:
                    if not (ta <= t <= te):
                        continue
                    dv = [p[i] - G[i] for i in range(3)]
                    s_f = sum(dv[i] * F[i] for i in range(3))
                    lat = math.sqrt(max(sum(x * x for x in dv) - s_f * s_f, 0.0))
                    ok = lat <= 3.0 * r and t - ta > 60000  # (the arrival's own 0x21 / 0x1e exit excluded: its first 60 s)
                    if prev and ok and prev[1] and (prev[0] < 0) != (s_f < 0) and (tj is None or tj - t > 5000):
                        cross.append('%.1f' % (t / 1000))
                    prev = (s_f, ok)
            w = sum(1 for t, gg, wk in lane[s] if gg == g and ta <= t <= te and wk not in ('none', '-'))
            entries = sum(1 for t, gg, fr, to, why, st in ctl[s] if gg == g and ta <= t <= te and to in ('committed', 'escort'))
            tot_ep += len(ep)
            tot_cross += len(cross)
            tot_w += w
            tot_entry += entries
            lines.append('     %s arrived %.1f jump %s | 0x12 %d %s | plane crossings %d %s | traced rows %d | lane writes %d, board entries %d' % (
                s, ta / 1000, '%.1f' % (tj / 1000) if tj is not None else '-', len(ep), ep[:6], len(cross), cross[:6],
                sum(1 for t, p in trace[s] if ta <= t <= te), w, entries))
        t0 = min(ta for s, ta in trip)
        t1 = max(e for e in ends if e is not None) if all(e is not None for e in ends) else None
        print('gate %s return trip %.1f ships %d: span arrival -> last launch %s, 0x12 %d, plane crossings without a jump %d, lane writes %d, '
              'board entries as committed or escort %d%s' % (g, t0 / 1000, len(trip), '%.1f s' % ((t1 - t0) / 1000) if t1 else 'open', tot_ep,
                                                              tot_cross, tot_w, tot_entry,
                                                              ' (not vanilla: a build before the latch)' if tot_w or tot_entry else ''))
        for x in lines:
            print(x)
