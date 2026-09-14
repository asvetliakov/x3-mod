"""Parser for the capture-only `motion_route` session-log lines.

One line per scene draw of a captured frame (src/proxy/motion_output.cpp,
MotionOutput::after_draw), carrying the route verdict (`gate`, `routed`,
`matched`), the draw key, and the draw-state signature appended after
`result=`: `zwrite`, `blend`, `src`, `dst`, `atest`, `mask`, `sepalpha`,
`fog`. The state fields come from the proxy's render-state shadow, so a value
the shadow has not seen is logged as `-1` and parsed as None; lines written
before the fields existed simply lack them and parse with every state None.
`src`, `dst` and `sepalpha` are shadowed only while a composition producer is
requested (linear emission or distance fade) and are None otherwise.

The signature classifies a gate-4 (`DrawState`) refusal: which refused draws
are source-over blended, alpha-tested or masked, without a new capture.
"""
import re

FIELD = re.compile(r'(\w+)=([^\s]+)')
PREFIX = 'motion_route '
GATES = ('none', 'feature', 'scene', 'pair', 'draw_state', 'scope', 'history')
STATE_FIELDS = ('zwrite', 'blend', 'src', 'dst', 'atest', 'mask', 'sepalpha', 'fog')
# D3DBLEND values of the source-over pair the fade check requires.
SRCALPHA, INVSRCALPHA = 5, 6


def _state(row, name):
    """The shadowed value, or None when the field is absent or logged -1."""
    if name not in row:
        return None
    value = int(row[name])
    return None if value < 0 else value


class Route:
    __slots__ = ('device', 'frame', 'index', 'gate', 'routed', 'matched', 'depth',
                 'jittered', 'vs', 'ps', 'state')

    def __init__(self, row):
        self.device = int(row['device']); self.frame = int(row['frame']); self.index = int(row['index'])
        self.gate = int(row['gate'])
        self.routed = int(row['routed']) != 0
        self.matched = int(row.get('matched', 0)) != 0
        self.depth = int(row.get('depth', 0)) != 0
        self.jittered = int(row.get('jittered', 0)) != 0
        self.vs = row.get('vs'); self.ps = row.get('ps')
        self.state = {name: _state(row, name) for name in STATE_FIELDS}
        if not 0 <= self.gate < len(GATES):
            raise ValueError(f'unknown gate {self.gate}')

    @property
    def gate_name(self):
        return GATES[self.gate]

    @property
    def pair(self):
        return (self.vs, self.ps)

    @property
    def blended(self):
        """True/False from the shadow, None when ALPHABLENDENABLE is unknown."""
        blend = self.state['blend']
        return None if blend is None else blend != 0

    @property
    def source_over(self):
        """Blended with SRCALPHA/INVSRCALPHA; None while any of the three is unknown."""
        if self.blended is None:
            return None
        if not self.blended:
            return False
        src, dst = self.state['src'], self.state['dst']
        return None if src is None or dst is None else (src == SRCALPHA and dst == INVSRCALPHA)

    @property
    def alpha_tested(self):
        test = self.state['atest']
        return None if test is None else test != 0

    @property
    def signature(self):
        """Compact state signature for grouping a census; `?` for unknown."""
        return ' '.join(f'{n}={"?" if self.state[n] is None else self.state[n]}' for n in STATE_FIELDS)


def parse(lines):
    """Route records in log order; non-matching lines are ignored."""
    return [Route(dict(FIELD.findall(line[len(PREFIX):]))) for line in lines if line.startswith(PREFIX)]


def by_frame(records):
    """{(device, frame): [records]} in log order."""
    result = {}
    for r in records:
        result.setdefault((r.device, r.frame), []).append(r)
    return result


def census(records, gate=4):
    """{(vs, ps, signature): count} over the records refused at `gate`."""
    result = {}
    for r in records:
        if r.gate == gate:
            key = (r.vs, r.ps, r.signature)
            result[key] = result.get(key, 0) + 1
    return result
