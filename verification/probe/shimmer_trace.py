"""Parser for the distant-shimmer trace lines (X3M_SHIMMER_TRACE=1).

Reads the `shimmer_frame` / `shimmer_draw` pairs a session log carries with
the trace on (src/proxy/motion_output.cpp, docs/architecture/
linear-distance-fade-region.md, "Shimmer trace (diagnostic)") and returns one
record per frame: the frame's TAA state, the projection terms (p00/p11, stored
in the log as integers scaled by 1e4), the Asteroid-class draws it logged, the
truncated count, and — against the previous frame of the same device — which
draw identities changed LOD or disappeared.
"""
import re

SCALE = 10000.0
FIELD = re.compile(r'(\w+)=([^\s]+)')


def fields(line):
    return dict(FIELD.findall(line))


def _int(row, name, default=0):
    try:
        return int(row[name])
    except (KeyError, ValueError):
        return default


class Frame:
    def __init__(self, row):
        self.device = _int(row, 'device')
        self.frame = _int(row, 'frame')
        self.draws = _int(row, 'draws')
        self.asteroid = _int(row, 'asteroid')
        self.logged = _int(row, 'logged')
        self.truncated = _int(row, 'truncated')
        self.taa = _int(row, 'taa')
        self.taa_history = _int(row, 'taa_history')
        self.taa_skip = _int(row, 'taa_skip')
        self.cut = _int(row, 'cut')
        self.camera_cut = _int(row, 'camera_cut')
        self.jitter_index = _int(row, 'jitter_index')
        self.history_previous = _int(row, 'history_previous')
        self.history_current = _int(row, 'history_current')
        self.committed = _int(row, 'committed')
        self.camera_valid = _int(row, 'camera_valid')
        self.p00 = _int(row, 'p00_e4') / SCALE
        self.p11 = _int(row, 'p11_e4') / SCALE
        self.draws_logged = []
        self._previous = None
        # Filled by parse(): identities whose lod changed against the previous
        # traced frame of the same device, and identities that vanished.
        self.lod_changes = []
        self.disappeared = []
        self.appeared = []
        # Draws whose key the route never resolved (node 0 or model 0, e.g. a
        # gate-failed draw): counted, never tracked as an identity, because
        # they would all collapse into one and fabricate changes.
        self.unidentified = 0

    @property
    def history_dropped(self):
        """The frame ran the resolve without reusing history."""
        return bool(self.taa) and not self.taa_history

    def identities(self):
        return {(d.node, d.model): d for d in self.draws_logged if d.node and d.model}


class Draw:
    def __init__(self, row):
        self.device = _int(row, 'device')
        self.frame = _int(row, 'frame')
        self.index = _int(row, 'index')
        self.gate = _int(row, 'gate')
        self.routed = _int(row, 'routed')
        self.composition = _int(row, 'composition')
        self.node = _int(row, 'node')
        self.model = int(row['model'], 16) if 'model' in row else -1   # logged as %08lx
        self.lod = int(row['lod'], 16) if 'lod' in row else -1
        self.vertex_buffer = _int(row, 'vb')
        self.index_buffer = _int(row, 'ib')
        self.topology = _int(row, 'topology')
        self.indexed = _int(row, 'indexed')
        self.vertex_count = _int(row, 'vertex_count')
        self.index_count = _int(row, 'index_count')
        self.primitives = _int(row, 'primitives')
        f = _int(row, 'f_permille', -1)
        self.fade_admitted = f >= 0
        self.f = None if f < 0 else f / 1000.0
        self.region_known = _int(row, 'region')
        self.rect = tuple(int(v) for v in row['rect'].split(',')) if 'rect' in row else None


def parse(lines):
    """Returns the list of Frame records in log order.

    Lines other than shimmer_frame/shimmer_draw are ignored; a shimmer_draw
    without its frame line is attached to nothing and dropped.
    """
    frames, current = [], {}
    for line in lines:
        if 'shimmer_frame ' in line:
            frame = Frame(fields(line))
            previous = current.get(frame.device)
            if previous is not None and previous.frame + 1 == frame.frame:
                frame._previous = previous
            current[frame.device] = frame
            frames.append(frame)
        elif 'shimmer_draw ' in line:
            draw = Draw(fields(line))
            frame = current.get(draw.device)
            if frame is not None and frame.frame == draw.frame:
                frame.draws_logged.append(draw)
    for frame in frames:
        frame.unidentified = sum(1 for d in frame.draws_logged if not d.node or not d.model)
        previous = frame._previous
        if previous is None:
            continue
        before, after = previous.identities(), frame.identities()
        for key, draw in after.items():
            if key in before and before[key].lod != draw.lod:
                frame.lod_changes.append((key, before[key].lod, draw.lod))
            elif key not in before:
                frame.appeared.append(key)
        frame.disappeared = [key for key in before if key not in after]
    return frames
