"""Parser for the `ship_trace` rows of x3m.log (ship_steering.cpp, X3M_SHIP_TRACE=1 under --debug).

One row per frame per followed ship:

    ship_trace frame=<boundary frame> t_ms=<game ms> obj=0x<ship> class=<SG_SH name> order=0x<f+0x258> tgt=0x<f+0x224>
      tgt_class=<[tgt+0x48]> pos=x,y,z (units, phys+0x30) fwd=x,y,z up=x,y,z (the flight basis rows 2 and 1, unit)
      rate=y,p,r (obj+0x24.., binary angle/s) v=<obj+0x10> des=<obj+0x14 after the module's site-B write>
      des_engine=<obj+0x14 as the engine set it this frame> (units/s)
      point=x,y,z (the point the module handed back, units) given=x,y,z (the point the engine passed to the leaf;
      both = f+0x240 when no leaf call happened this frame) e=y,p,r (the remainders the module wrote, binary; `-` when
      no leaf call) dt_ms=<[root+0x714]> W=y,p,r (the rate caps the getters returned, binary/s) rot=0x<f+0x2b0>
      rot_pct=<record +0xd0> vmax=<0x00450980> acc=<0x00450b80> (units/s, units/s^2)
      emg=<0|1: the avoidance braking ramp was active at the last leaf call> omega_cmd=y,p,r (the commanded rate per
      axis at the last leaf call, deg/s) obst=0x<the current detour's obstacle, 0 = none>
      slip=x,y,z (amendment 14: obj+0x60..+0x68 at site B, units/s: the integrator's slip, the velocity is v fwd + slip)
      cap_ms=<the traffic controller's yield cap, m/s; -1 none> cap_age_ms=<its age, ms; -1 none> (stage B, Run161 item 4)

The replay tool feeds a row's inputs (pos, fwd, up, rate, v, des, given, dt, W, rot, rot_pct, vmax, acc) to the
fixture's engine model and compares the model's next state with the next row of the same ship (the oracle).
"""
import re

_ROW = re.compile(
    r'^ship_trace frame=(?P<frame>\d+) t_ms=(?P<t_ms>\d+) obj=0x(?P<obj>[0-9a-f]{8}) class=(?P<cls>\S+) order=0x(?P<order>[0-9a-f]+) '
    r'tgt=0x(?P<tgt>[0-9a-f]{8}) tgt_class=(?P<tgt_class>\d+) pos=(?P<pos>-?\d+,-?\d+,-?\d+) '
    r'fwd=(?P<fwd>-?[\d.]+,-?[\d.]+,-?[\d.]+) up=(?P<up>-?[\d.]+,-?[\d.]+,-?[\d.]+) rate=(?P<rate>-?\d+,-?\d+,-?\d+) '
    r'v=(?P<v>-?\d+) des=(?P<des>-?\d+) des_engine=(?P<des_engine>-?\d+) point=(?P<point>-?\d+,-?\d+,-?\d+) given=(?P<given>-?\d+,-?\d+,-?\d+) '
    r'e=(?P<e>-|-?\d+,-?\d+,-?\d+) dt_ms=(?P<dt_ms>\d+) W=(?P<W>-?\d+,-?\d+,-?\d+) rot=0x(?P<rot>[0-9a-f]{8}) '
    r'rot_pct=(?P<rot_pct>-?\d+) vmax=(?P<vmax>-?\d+) acc=(?P<acc>-?\d+)'
    r'(?: emg=(?P<emg>[01]) omega_cmd=(?P<omega_cmd>-?[\d.]+,-?[\d.]+,-?[\d.]+|nan,nan,nan) obst=0x(?P<obst>[0-9a-f]{8})'
    r'(?: slip=(?P<slip>-?\d+,-?\d+,-?\d+)(?: cap_ms=(?P<cap>-?[\d.]+) cap_age_ms=(?P<cap_age>-?\d+))?)?)?$')

INT3 = ('pos', 'rate', 'point', 'given', 'W')
FLOAT3 = ('fwd', 'up', 'omega_cmd')
INTS = ('frame', 't_ms', 'tgt_class', 'v', 'des', 'des_engine', 'dt_ms', 'rot_pct', 'vmax', 'acc', 'emg')
HEX = ('obj', 'order', 'tgt', 'rot', 'obst')


def parse_row(line):
    """The row as a dict, or None when the line is not a ship_trace row."""
    m = _ROW.match(line.strip())
    if not m:
        return None
    d = m.groupdict()
    out = {'class': d['cls']}
    # The emg/omega_cmd/obst suffix is optional (rows before Run156 lack it), and its slip field (rows before amendment 14).
    for k in INTS:
        out[k] = int(d[k]) if d[k] is not None else None
    for k in HEX:
        out[k] = int(d[k], 16) if d[k] is not None else None
    for k in INT3:
        out[k] = tuple(int(v) for v in d[k].split(','))
    for k in FLOAT3:
        out[k] = tuple(float(v) for v in d[k].split(',')) if d[k] is not None else None
    out['slip'] = tuple(int(v) for v in d['slip'].split(',')) if d['slip'] is not None else None
    # the traffic controller's yield cap and its age (stage B; rows before it lack them)
    out['cap'] = float(d['cap']) if d['cap'] is not None else None
    out['cap_age_ms'] = int(d['cap_age']) if d['cap_age'] is not None else None
    out['e'] = None if d['e'] == '-' else tuple(int(v) for v in d['e'].split(','))
    out['leaf_call'] = out['e'] is not None
    return out


def parse_rows(lines):
    """Every ship_trace row of an iterable of lines, in order."""
    rows = []
    for line in lines:
        r = parse_row(line)
        if r is not None:
            rows.append(r)
    return rows


def by_ship(rows):
    """Rows grouped per ship, each group in row order: {obj: [rows]}."""
    groups = {}
    for r in rows:
        groups.setdefault(r['obj'], []).append(r)
    return groups


# Amendment 16 (capital-gate-transit.md section 16.5): the key=value diagnostic rows of a capital crowd at a gate.
#   ship_capital_lane frame= A= B= verdict=a|b|c|s gap_m= w_A= w_B= w_src=<A>,<B> foot=<A>,<B> e_m= theta_deg= ds_m= v_cap= D_m=
#     route=foot|corr|lat|along|- watch= lat_gap= mot= h_T= h_L= trail= bind= held=   (amendment 16b, section 16.10.6; held = A in lane
#     by the hysteresis of section 16.11 only; amendment 17: v_cap / D_m / bind for a (b) trailer only, Dx= and seq_cap= gone)
#   ship_gate_capital_latch frame= obj= class= slot= event=pred_none|slow_join|unlatch from= to= v_cmd= s_m= e_m= psi_deg= upsi_deg=
#     w_dps= v_ms= W_dps= alpha_dps2= a_ms2= r_gate_m= cands_v:grade:nose:vel:e:over:e_max:reached:steps=<4 x, |-separated>
#     obstacle= lanes_other:verdict:gap=<|-separated, or ->   (amendment 17: steps added, seq_cap= seq_leader= gone; logs before it
#     carry cands_v:grade:nose:vel:e:over:e_max:reached and seq_cap= seq_leader=)
#   ship_capital_census frame= type= class= r_m= w_m= w_r= w_src=unread|lod|fail retries= l_m= h_rw_m= (sqrt(r^2 + w^2)) len_m= (max(r, l))
#     h_m= (sqrt(len^2 + w^2), the h the rule uses) part_id= part_lod= o_x= o_y= e_x= e_y= (amendment 17: the widest part's body id,
#     its widest LOD record, its lateral offset bound and half-extents, m)
#     l_r= capsule_L_m= (the gate traffic controller: l / r and the capsule half-length max(r, l))
# The gate traffic controller, stage A (gate-traffic-controller.md D.7, A.1; ship_steering_gates_traffic, --debug). The formats
# below are the DLL's since the stage B deletion (2026-10-15). Parsing is by row name and key=value, so an older log still parses: the
# deleted ship_gate_ctl_seq row (LEGACY_KV_ROWS) and the deleted member fields (petal= rho= s= stage_s= d_join= t_rel= ringed=) come
# through as extra keys, and a field a row lacks is simply absent from its dict.
#   ship_gate_ctl frame= t_ms= gate= ship= id= seq= key= order= from= to=none|committed|escort|arrival|evicted why= rank=
#     stage=<s>><s> (the member's lane stage none|funnel|handover|dock|launch|inbound, the same on both sides: role transitions only)
#   ship_gate_ctl_pair frame= t_ms= gate= a= b= seq=<a>,<b> rank=<a>,<b> kind=verdict|e_pass|order0 verdict=none|clear|unresolved
#     before= why=- or the E reason none|unresolved|stale|starved|on_0x12|rows|order0|not_member t_c_s= (-1: none) yielder=none|a|b
#     cap_ms= (-1: none) d_m=   (verdict: a member pair's verdict change, at most once per pair per second; e_pass: site E passed the
#     pair, at most once per pair per second; order0: an order-0 frame of a member, each partner within 5 km)
#     B.9: cause=nocert|cert|yield|contact|budget|stale|zero|noyield (the pair's last verdict write: no sweep / clear on both plans /
#     clear after a yield / a conflict no yield candidate cleared / a conflict whose yield was not tried / rollouts too far apart or too
#     short / a rollout without samples / a conflict whose yielder could not yield: no steered order or an empty ladder) trig=clock|plan
#     (plan: the plan of the member whose rollout wrote the entry changed since its previous rollout) gap_m= (the sweep's minimum gap
#     over the stored rollouts, capsule gap minus margin, m; < 0 below the margin; na: no rollout pair or too short an overlap)
#   ship_gate_launch frame= t_ms= ship= gate= class= capital= slot= gate_type= kind=vanilla|capital_offset|small_side|small_unseparated
#     p_m=<x>,<y>,<z> off_r_m= off_u_m= r_gate_m= r_m= w_m=   (one per launch at 0x00444606)
# The docking approach, amendment 18 (docking-approach.md; ship_steering_dock, --debug), 1 Hz per ship on order 4 / 0x1d to a container:
#   ship_avoid_dock frame= t_ms= ship= cont= class= order= parts=0|1 n_parts= path_m= ray_m= (-1: no model) margin_m= (1.2 r + 50 m)
#     reach_m= e=suppress_dock|pass_dock_parts|pass_dock_path|pass_dock_ray|pass_dock_zone|- (site E's row now; - off order 4 or within
#     0.8 r of a carrier) zone=out|near|in|inner (> 2 r_C, > r_C, <= r_C, a carrier within 0.8 r_C) d_c_m= r_c_m= v_ms= kept= detour=
#   ship_gate_ctl_member frame= t_ms= gate= ship= class= role= stage= (the lane stage, as above) order= target= p_m=<R>,<U>,<F> (the last
#     state read, gate frame, m) nose_f= (the nose . F) v_ms= key= traced=0|1 st_age_ms= dec_age_ms= (-1 none)   (B.4: one per board
#     member per game second, traced or not; ring 32, drops in rows_dropped's ctl) live=0|1 (B.5: the member passed the liveness proof
#     this frame; 0: no engine field read, class=?) zx= (X along F, m)
# Lane stage 1 (convoy-gate-transit.md section 6.9; ship_steering_gates_lane, --debug): after each member row of a lane gate
#   ship_gate_ctl_lane frame= t_ms= gate= ship= lane_s= (along F, m) rank= (the lane order, 0 the head; 255 none) gap_m= (tail of the ship
#     ahead - own nose, m; na none) law=none|join|lane|merge|follow|stop|pace|gate|hold lat_m= (lateral distance from Lambda, m)
#     lc_m=<R>,<U> (Lambda's l_c, m) v_law= (the law's speed, m/s) v_w= (site B's last lane write, m/s; na none)
#     w=none|stop|kept|hold (that write's kind: a stop, P2's kept speed under the brake bound from P2, the order-0 hold; round A: a
#     hand-over P2 did not keep gets no write, the `unkept` kind of lane fix round 1 is gone) on=0|1 (on Lambda) merged=0|1 (the merge's
#     completion, the raw gap >= g_req - 50 m) loop=none|leg|out|turn (round C's loop-back join after a flown 0x12: onto its out-leg,
#     out along it to the turn point, the U-turn past it; review of rounds A-C item 9)
#   ship_gate_ctl_merge frame= t_ms= gate= ship= pred= event=start|finish gap_m= law= dur_ms= (finish: since the start)
#   ship_gate_ctl_rewrite frame= t_ms= gate= ship= site=a|b|adopt|miss slot= (the KC grid slot the field held; -1 none) from_m=<R>,<U>,<F>
#     to_m=<R>,<U>,<F> (the order-4 field before and after, gate frame, m): a: site A's rewrite of a grid point to L_i; b: site B's on a
#     deferred first tick; adopt: a field equal to the hull's own L_i taken as ours without a lane member (a load, a slot reset); miss: a
#     lane member's field that is neither a grid point nor ours (once per value)   (review items 2, 4, 9)
#   ship_gate_ctl_defer frame= t_ms= gate= ship= order= kind=defer|restore|rewrite f210_read= f210_written= ahead_ms= (the tick
#     deferral per ship: an order-0 write of f+0x210 = now + the deferral, the episode's first and then one per game second; the next
#     order's first frame putting it back, with the grid point rewritten to L_i; lane fix round 1 item 5)
#   ship_steering_lane_summary ... ctl_grid_miss= ctl_point_adopts= ctl_lane_defers= ctl_defer_rewrites= (totals since install)
#     ctl_recommit_held= (commits held by the re-commit hysteresis, item 4) gates_gate:F:G:r=<gate>:<F>:<G>:<r_gate>|... (each lane gate's
#     frame: F the axis the lane's s runs along, G, r; '-' none; item 5)
#   ship_avoid_engine_warn_pre ... gate_row= (the own-gate row's reason, gc::GateRow, of the first gate entry's last site E verdict: lane
#     = suppressed by the lane row, roll = its rollout stale or not clear beyond 6 r_gate, ...; '-' none; item 5)
#   (ship_gate_ctl_pair cause= also takes merge (a lane yielder cleared on the 10 m/s floor) and stop (on the brief stop))
# Lane fix round 2 (convoy-gate-transit.md section 11 "Lane fix round 2"):
#   ship_avoid_e_verdict ... verdict=suppress_lane_obst (a lane member's path clears a non-member obstacle) obst_row=no|lane|roll
#     step_ms= (the lane obstacle row's reason for B = a station, an asteroid or a ship that is not a board partner: no = not a lane
#     member on a lane order or the player's ship, lane = suppressed, roll = the path does not clear by the sweep margin or its rollout is
#     stale; item 1)
#   ship_avoid_engine_warn_pre ... obst_row= (the lane obstacle row's reason of the first non-gate entry's last verdict; '-' none)
#   ship_steering_lane_summary ... e_lane_obst= e_lane_obst_roll= (site E's lane obstacle row: suppressions, passes on roll)
# The arrival round (convoy-gate-transit.md section 11 "Arrival round (Run174)"):
#   ship_avoid_e_verdict / ship_avoid_engine_warn_pre ... gate_row= also escort|arrival|other (site E's ring row suppressed the engine's
#     warn of an AI ship against a gate on any order: its predicted path with its hull clears the gate's solid; none without a collision
#     tree), refused_hull (the hull would reach the solid on the path: the warn kept), roll (unreadable or unknown: kept); verdict=
#     suppress_gate_ring (item 4); the member row's own kinds as before (suppress / lane = member)
#   ship_steering_lane_summary ... e_gate_ring= e_gate_ring_kept= (the ring row's suppressions and kept warns since install; item 4)
#   ship_player_pos frame= t_ms= obj= pos=<x>,<y>,<z> gate= d_gate_m= member= d_member_m= (once per report while the piloted ship is
#     within 15 km of a gate: its position, the nearest gate, the nearest board member, m; -1 none; item 6)
# Review of the arrival round (convoy-gate-transit.md section 11 "Arrival round review fixes"):
#   ship_gate_ring_read frame= t_ms= gate= id= type= board= state=unknown|none|tree why= nodes= scene= seen= verdict=none|ring (--debug,
#     once per gate: site E's collision-tree read under the positive guard; why = the first guard that failed (ok, gate_ptr, class, root,
#     scene, node_ptr, parent, chain, depth, budget, no_scene_node); verdict none = no solid, ring = the TGates row's ring; item 1)
#   ship_steering_lane_summary ... ctl_mouth_expired= (gate-mouth holds ended by their game-time bound; item 3)
#   ship_avoid_e_verdict ... verdict=pass_player (B is the player's ship: no suppression row; item 2)
# Run175 round (convoy-gate-transit.md section 11 "Run175 round"):
#   ship_gate_ctl_member ... w_m= (the hull's lateral half-width, m) w_ok=0|1 (it came from a read; 0: unread, a body for the sweep only)
#     funnel=0|1 (inside the join funnel; item 3)
#   ship_gate_ctl_lane ... pt=lane|point (point: the member stands on an unrewritten script point, its lateral the member's lane lateral,
#     no lane steer; item 1) own=0|1 (the lane owns the member: its laws, writes and point apply; 0 outside the funnel, width unread, or
#     on an unrewritten point)
#   ship_gate_ctl_rewrite ... r_m= (the hull's radius) implied_m= (the size the field's depth implies, 2 (r_gate + r'); -1 off the
#     20-slot lateral pattern) kc_m= (the size the KC's GetSize reads, the render root's cached subtree radius [[obj+0x70]+0xa4]; -1
#     unreadable or dirty)   (item 1: a script point of any size within 0.6-1.6 r is rewritten; a miss row carries the three)
#   ship_gate_ring_node frame= t_ms= gate= i= depth= scene= f25c= f140= flags= (--debug, once per gate whose first read visited more than
#     3 nodes: each visited node's depth and fields [c+0x258], [c+0x25c], [c+0x140], [c+0x12c]; item 6)
#   ship_gate_ring_reread frame= t_ms= gate= id= nodes_before= nodes= state_before= state= changed=0|1 (every 60 s + the step per gate on
#     the warn path: the node count before and after; the verdict stays the first one unless a tree appears; item 6)
#   ship_steering_handler_slow frame= t_ms= site=A obj= leaf= order= us= cycles= snap_walk=0|1 snap_us= (--debug with timing: a handler
#     call beyond 500 us: its QPC delta, its QueryThreadCycleTime delta, whether snapshot_for walked the sector and that walk's us; item 6)
# Run176 diagnostics (convoy-gate-transit.md section 11 "Run176 diagnostics"):
#   ship_steering_handler_slow ... cyc_dyn= cyc_find= cyc_avoid= cyc_gate= cyc_steer= cyc_ctl= cyc_tail= (thread cycles per decide() phase,
#     0 not reached) prev_order= (the table entry's order before the call; 0xffff not reached) new_slot=0|1 (the entry had no previous call)
#     snap_n= conflicts= (avoid_adjust's snapshot count and av::resolve's conflicts; -1 not reached; item 1)
#   ship_steering_summary ... ctl_decide_cyc_max= ctl_decide_max_gate= ctl_decide_max_ship= ctl_decide_max_id= (the ctl_decide_us_max holder's
#     thread cycles, its gate and member; item 1)
#   ship_gate_ring_read ... parts= effects= (the visited nodes but the effect nodes; effect nodes: scene id 0 and part index 0)
#     node_rows= (the ship_gate_ring_node rows queued for the read, its first 8 nodes at most; item 2)
LEGACY_KV_ROWS = ('ship_gate_ctl_seq',)  # rows the DLL no longer prints (stage B's sequencer, deleted 2026-10-15); still parsed in old logs
KV_ROWS = ('ship_capital_lane', 'ship_gate_capital_latch', 'ship_capital_census', 'ship_gate_ctl', 'ship_gate_ctl_pair', 'ship_gate_launch',
           'ship_avoid_dock', 'ship_gate_ctl_member', 'ship_gate_ctl_lane', 'ship_gate_ctl_merge', 'ship_gate_ctl_rewrite',
           'ship_gate_ctl_defer', 'ship_steering_lane_summary', 'ship_avoid_engine_warn_pre', 'ship_player_pos',
           'ship_gate_ring_read', 'ship_gate_ring_node', 'ship_gate_ring_reread', 'ship_steering_handler_slow') + LEGACY_KV_ROWS
LIST_FIELDS = ('cands_v:grade:nose:vel:e:over:e_max:reached', 'cands_v:grade:nose:vel:e:over:e_max:reached:steps', 'lanes_other:verdict:gap',
               'gates_gate:F:G:r', 'entries_other:class:code:order:leader:member:d_m:r_m:e_verdict')  # (lane fix round 2 item 6: the summary's
#                gate frames, a vector item a tuple; the warn pre-row's entries)
_KV = re.compile(r'(\S+?)=(\S*)')
_INT = re.compile(r'-?\d+')
_FLOAT = re.compile(r'-?\d+\.\d*|-?\d*\.\d+')


def _scalar(text):
    if text.startswith('0x'):
        return int(text, 16)
    if _INT.fullmatch(text):
        return int(text)
    if _FLOAT.fullmatch(text):
        return float(text)
    return text


def _kv_value(key, text):
    if key in LIST_FIELDS:
        if text == '-':
            return []
        return [tuple(tuple(_scalar(t) for t in p.split(',')) if ',' in p else _scalar(p) for p in item.split(':')) for item in text.split('|')]
    if ',' in text:
        return tuple(_scalar(t) for t in text.split(','))
    return _scalar(text)


def parse_kv_row(line):
    """An amendment-16 row as {'row': name, field: value, ...} (hex and numbers converted, the list fields split), or None."""
    line = line.strip()
    name = line.split(' ', 1)[0]
    if name not in KV_ROWS:
        return None
    out = {'row': name}
    for key, value in _KV.findall(line[len(name):]):
        out[key] = _kv_value(key, value)
    return out


def parse_kv_rows(lines, name=None):
    """Every amendment-16 row of an iterable of lines (optionally of one row name), in order."""
    rows = []
    for line in lines:
        r = parse_kv_row(line)
        if r is not None and (name is None or r['row'] == name):
            rows.append(r)
    return rows
