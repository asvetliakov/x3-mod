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
    r'(?: slip=(?P<slip>-?\d+,-?\d+,-?\d+))?)?$')

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
# The gate traffic controller, stage A (gate-traffic-controller.md D.7, A.1; ship_steering_gates_traffic, --debug):
#   ship_gate_ctl frame= t_ms= gate= ship= id= seq= order= from= to=none|committed|escort|arrival|evicted why= rank=   (role transitions)
#   ship_gate_ctl_pair frame= t_ms= gate= a= b= seq=<a>,<b> rank=<a>,<b> verdict=none|clear|unresolved before= t_c_s= (-1: none)
#     yielder=none|a|b cap_ms= (-1: none) d_m=   (a member pair's verdict change, at most once per pair per second)
#   ship_gate_launch frame= t_ms= ship= gate= class= capital= slot= gate_type= kind=vanilla|capital_offset|small_side|small_unseparated
#     p_m=<x>,<y>,<z> off_r_m= off_u_m= r_gate_m= r_m= w_m=   (one per launch at 0x00444606)
# The docking approach, amendment 18 (docking-approach.md; ship_steering_dock, --debug), 1 Hz per ship on order 4 / 0x1d to a container:
#   ship_avoid_dock frame= t_ms= ship= cont= class= order= parts=0|1 n_parts= path_m= ray_m= (-1: no model) margin_m= (1.2 r + 50 m)
#     reach_m= e=suppress_dock|pass_dock_parts|pass_dock_path|pass_dock_ray|pass_dock_zone|- (site E's row now; - off order 4 or within
#     0.8 r of a carrier) zone=out|near|in|inner (> 2 r_C, > r_C, <= r_C, a carrier within 0.8 r_C) d_c_m= r_c_m= v_ms= kept= detour=
KV_ROWS = ('ship_capital_lane', 'ship_gate_capital_latch', 'ship_capital_census', 'ship_gate_ctl', 'ship_gate_ctl_pair', 'ship_gate_launch',
           'ship_avoid_dock')
LIST_FIELDS = ('cands_v:grade:nose:vel:e:over:e_max:reached', 'cands_v:grade:nose:vel:e:over:e_max:reached:steps', 'lanes_other:verdict:gap')
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
        return [tuple(_scalar(p) for p in item.split(':')) for item in text.split('|')]
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
