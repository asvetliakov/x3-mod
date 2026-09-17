#!/usr/bin/env python3
"""Parser and summary of the per-frame sun trace
(launcher --shadow-sun-trace, X3M_SHADOW_SUN_TRACE=1; default off).

One line kind, read from a capture log line by line (never loaded whole):

  shadow_sun_frame device= frame= source= reason= poll= rederived=
      rederived_mask= carried= checks= disagreements= agreement_deg=
      distance= cascades=

`source` is point or latch, `reason` the PointSunReason that decided it,
`poll` the sun_light_poll status, `rederived_mask` a bit per cascade
(bit k = cascade k re-derived its held direction this frame) and
`agreement_deg` the worst poll/constant angle of the frame (-1: no check).
A malformed line raises MalformedLine. `summary()` answers what the sparse
`shadow_replay_sun_point` lines cannot: the re-derivation rate per cascade
over the traced frames, split by whether the frame re-derived anything,
and the source/reason mix.
"""
import json
import re
import statistics
import sys
from pathlib import Path

PREFIX = 'shadow_sun_frame '
FIELDS = ('device', 'frame', 'source', 'reason', 'poll', 'rederived', 'rederived_mask', 'carried',
          'checks', 'disagreements', 'agreement_deg', 'distance', 'cascades')
INT_FIELDS = ('device', 'frame', 'rederived', 'rederived_mask', 'carried', 'checks', 'disagreements', 'cascades')
FLOAT_FIELDS = ('agreement_deg', 'distance')
SOURCES = ('point', 'latch')
CASCADE_MAX = 5  # renderer::shadow_cascade_max
FIELD = re.compile(r'(\w+)=(\S+)')


class MalformedLine(ValueError):
    pass


def parse_line(line):
    """One shadow_sun_frame line into a row; the field list and order are exact."""
    body = line[len(PREFIX):].strip()
    pairs = FIELD.findall(body)
    if [k for k, _ in pairs] != list(FIELDS) or ' '.join(f'{k}={v}' for k, v in pairs) != body:
        raise MalformedLine(line.rstrip('\n'))
    row = dict(pairs)
    try:
        for name in INT_FIELDS:
            row[name] = int(row[name])
        for name in FLOAT_FIELDS:
            row[name] = float(row[name])
    except ValueError as error:
        raise MalformedLine(line.rstrip('\n')) from error
    if row['source'] not in SOURCES or not 0 <= row['cascades'] <= CASCADE_MAX:
        raise MalformedLine(line.rstrip('\n'))
    if row['rederived_mask'] >> row['cascades'] or bin(row['rederived_mask']).count('1') != row['rederived']:
        raise MalformedLine(line.rstrip('\n'))
    if min(row['rederived'], row['carried'], row['checks'], row['disagreements']) < 0 or not row['distance'] >= 0.0:
        raise MalformedLine(line.rstrip('\n'))
    row['cascade_rederived'] = [bool(row['rederived_mask'] >> k & 1) for k in range(row['cascades'])]
    return row


def parse_lines(lines):
    return [parse_line(line) for line in lines if line.startswith(PREFIX)]


def summary(rows):
    """Rates over the traced frames: the source and reason mix, the frames that
    re-derived at least one cascade, the per-cascade re-derivation rate, and the
    agreement/distance spread of the frames that had a checked poll."""
    if not rows:
        return {'frames': 0}
    cascades = max(r['cascades'] for r in rows)
    frames = len(rows)
    rederived_frames = sum(1 for r in rows if r['rederived'])
    checked = [r for r in rows if r['checks']]
    agreements = [r['agreement_deg'] for r in checked if r['agreement_deg'] >= 0.0]
    distances = [r['distance'] for r in rows if r['source'] == 'point' and r['distance'] > 0.0]
    out = {'frames': frames, 'devices': sorted({r['device'] for r in rows}),
           'frame_span': [min(r['frame'] for r in rows), max(r['frame'] for r in rows)],
           'cascades': cascades,
           'source': {name: sum(1 for r in rows if r['source'] == name) for name in SOURCES},
           'reason': {name: sum(1 for r in rows if r['reason'] == name) for name in sorted({r['reason'] for r in rows})},
           'poll': {name: sum(1 for r in rows if r['poll'] == name) for name in sorted({r['poll'] for r in rows})},
           'rederived_frames': rederived_frames, 'rederived_frame_rate': rederived_frames / frames,
           'rederivations': sum(r['rederived'] for r in rows),
           'cascade_rederived': [sum(1 for r in rows if r['rederived_mask'] >> k & 1) for k in range(cascades)],
           'cascade_rederived_rate': [sum(1 for r in rows if r['rederived_mask'] >> k & 1) / frames for k in range(cascades)],
           'carried_frames': sum(1 for r in rows if r['carried']),
           'checked_frames': len(checked), 'disagreeing_frames': sum(1 for r in rows if r['disagreements'])}
    out['agreement_deg'] = {'frames': len(agreements), 'median': statistics.median(agreements),
                            'max': max(agreements)} if agreements else None
    out['distance'] = {'median': statistics.median(distances), 'min': min(distances), 'max': max(distances)} if distances else None
    return out


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print('usage: shadow_sun_frame.py <capture log>', file=sys.stderr)
        return 2
    with Path(argv[0]).open(errors='replace') as handle:
        rows = parse_lines(handle)
    print(json.dumps(summary(rows), indent=1, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
