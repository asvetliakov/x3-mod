"""Parser for the capture-only `fade_refused_rect` session-log lines.

One line per recognised source-over draw of a fade pair that admission
refused (src/proxy/motion_output.cpp, record_fade_refused / log_fade_refused;
docs/architecture/linear-station-source-over.md, section 4). The draw is not
composed; the line carries the step-1 rectangle the route would have used so
the pixels of a refused port can be decoded from the frame's HDR capture and
compared with a routed sibling's. Integer fields only; `refused_total` is the
frame's count including draws past the 16-record capacity (those have no line).
"""
import re

FIELD = re.compile(r'(\w+)=([^\s]+)')
PREFIX = 'fade_refused_rect '
REFUSALS = ('pair', 'permission_scene', 'readiness', 'readers', 'frame_stop', 'preparation')


class RefusedRect:
    __slots__ = ('device', 'frame', 'index', 'refusal', 'vs', 'ps', 'node', 'model', 'lod',
                 'bound', 'reason', 'status', 'rect', 'f_permille', 'f_of', 'refused_total')

    def __init__(self, row):
        self.device = int(row['device']); self.frame = int(row['frame']); self.index = int(row['index'])
        self.refusal = int(row['refusal'])
        self.vs = row['vs']; self.ps = row['ps']
        self.node = int(row['node']); self.model = int(row['model'], 16); self.lod = int(row['lod'], 16)
        self.bound = int(row['bound']) != 0; self.reason = int(row['reason']); self.status = row['status']
        self.rect = tuple(int(v) for v in row['rect'].split(','))
        self.f_permille = int(row['f_permille']); self.f_of = row['f_of']
        self.refused_total = int(row['refused_total'])
        if len(self.rect) != 4 or self.rect[0] >= self.rect[2] or self.rect[1] >= self.rect[3]:
            raise ValueError(f'empty or malformed rectangle {row["rect"]}')
        if len(self.vs) != 16 or len(self.ps) != 16 or not (0 <= self.refusal <= 5) or not (0 <= self.f_permille <= 1000):
            raise ValueError('malformed fade_refused_rect fields')
        if self.f_of not in ('viewport', 'target'):
            raise ValueError(f'unknown denominator {self.f_of}')

    @property
    def refusal_name(self):
        return REFUSALS[self.refusal]

    @property
    def area(self):
        return (self.rect[2] - self.rect[0]) * (self.rect[3] - self.rect[1])


def parse(lines):
    """RefusedRect records in log order; non-matching lines are ignored."""
    return [RefusedRect(dict(FIELD.findall(line[len(PREFIX):]))) for line in lines if line.startswith(PREFIX)]


def by_frame(records):
    """{(device, frame): [records]} in log order."""
    result = {}
    for r in records:
        result.setdefault((r.device, r.frame), []).append(r)
    return result
