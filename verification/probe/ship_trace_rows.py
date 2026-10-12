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
