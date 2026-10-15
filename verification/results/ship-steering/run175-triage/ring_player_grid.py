# Run175 Q2/Q3/Q4: (a) the ship_gate_ring_read rows; (b) site E's verdict= and gate_row=/obst_row= kinds (e_verdict rows, rate-limited) and
# the warn_pre gate_row=/obst_row=; engine warns against a gate (class 18); the lane summary's e_gate_ring / e_gate_ring_kept / grid
# counters; (c) ship_player_pos: rows, gate, d_gate range, nearest member, and any warn / e_verdict naming the player as B / other;
# (d) every ship_gate_ctl_rewrite with the KC size the field implies (|z|/2 - r_gate, slot 0..19 a/b ignored) next to the hull radius
# r_m of its ship_gate_launch / census row.  Usage: python3 -I ring_player_grid.py ROWS
import sys, re, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
ring = []; ev = C.Counter(); gr = C.Counter(); orow = C.Counter(); pre = C.Counter(); gwarn = 0; lane = {}; pp = []; rew = []; rl = {}
warn_other = C.Counter(); ev_b = C.Counter()
for l in open(sys.argv[1], errors='replace'):
    h = l.split(' ', 1)[0]
    if h == 'ship_gate_ring_read': ring.append(kv(l))
    elif h == 'ship_avoid_e_verdict':
        d = kv(l); ev[d.get('verdict')] += 1; gr[d.get('gate_row', '-')] += 1; orow[d.get('obst_row', '-')] += 1; ev_b[d.get('B')] += 1
    elif h == 'ship_avoid_engine_warn_pre':
        for k in ('gate_row', 'obst_row'):
            m = re.search(k + r'=(\S+)', l); pre[k + '=' + (m[1] if m else 'absent')] += 1
    elif h == 'ship_avoid_engine_warn':
        d = kv(l); gwarn += d.get('class') == '18'; warn_other[d.get('other')] += 1
    elif h == 'ship_steering_lane_summary': lane = kv(l)
    elif h == 'ship_player_pos': pp.append(kv(l))
    elif h == 'ship_gate_ctl_rewrite': rew.append(kv(l))
    elif h == 'ship_gate_launch':
        d = kv(l); rl.setdefault(d['ship'], []).append((int(d['t_ms']), d['class'], d['r_m']))  # (pointers are reused: the last launch before the rewrite)
print('(a) ring reads', len(ring))
for d in ring: print('  ', ' '.join('%s=%s' % (k, d[k]) for k in ('frame', 'gate', 'id', 'type', 'board', 'state', 'why', 'nodes', 'scene', 'seen', 'verdict')))
print('(b) e_verdict verdicts', dict(ev.most_common())); print('    gate_row', dict(gr)); print('    obst_row', dict(orow)); print('    warn_pre', dict(pre))
print('    engine warns against a gate (class 18):', gwarn, '| lane summary:', {k: lane.get(k) for k in ('e_gate_ring', 'e_gate_ring_kept', 'ctl_grid_miss', 'ctl_point_rewrites', 'ctl_point_adopts', 'ctl_mouth_expired')})
if pp:
    po = pp[0]['obj']; dg = [float(d['d_gate_m']) for d in pp]; dm = [(float(d['d_member_m']), d['member'], int(d['t_ms']) / 1000) for d in pp if float(d['d_member_m']) >= 0 and d['member'] != '0x00000000']
    print('(c) player_pos rows %d obj %s gates %s t %.1f..%.1f d_gate %.0f..%.0f; rows with a member %d, nearest %s' % (
        len(pp), po, dict(C.Counter(d['gate'] for d in pp)), int(pp[0]['t_ms']) / 1000, int(pp[-1]['t_ms']) / 1000, min(dg), max(dg), len(dm), min(dm) if dm else '-'))
    print('    engine warns with other=player: %d; e_verdict rows with B=player: %d; pass_player verdicts: %d' % (warn_other[po], ev_b[po], ev['pass_player']))
print('(d) order-4 field rewrites (site a|b|adopt|miss) and the KC size the field implies (r_gate 1104.5)')
for d in rew:
    z = -float(d['from_m'].split(',')[2]); L = [x for x in rl.get(d['ship'], []) if x[0] <= int(d['t_ms'])]; c, r = (L[-1][1], L[-1][2]) if L else ('?', '?')
    print('   t=%.1f ship=%s class=%s r_m=%s site=%s slot=%s from=%s implied_GetSize(ship)=%.0f' % (int(d['t_ms']) / 1000, d['ship'], c, r, d['site'], d['slot'], d['from_m'], z / 2 - 1104.5))
