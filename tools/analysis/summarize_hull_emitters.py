#!/usr/bin/env python3
"""Per-model table of the hull-program emitter draws of an F8 capture.

Joins the `hull_emission_draw` lines a capture frame writes for every admitted
ONE/ONE draw of the twelve covered hull programs (emitter plan phase 3) with
the `hull_emission_frame` accounting, so a session log answers which models
and LODs carry these emitters, through which program, how many draws and
primitives per frame, and where on screen their object origin projected. The
log is streamed line by line and never held whole. No game bytes are read.
"""
import argparse
from collections import defaultdict
import json
import re
import sys

FIELDS = re.compile(r'(\w+)=([^\s]+)')
PROGRAMS = ('5f82ecacd39529cd', '6733b119142c8d42', 'fffdabd910793aba', '496049cec2066ed3',
            'e6794b6ec37ff71a', 'f1b0e820c7b488c3', '0c1f3f0f440e4a0c', '7c83ed50c9894e44',
            '99153c144030c396', '64bac8bb307eb896', 'c1452981fd0bff64', 'e70adc744a38ca59')


def fields(line):
    return dict(FIELDS.findall(line))


def summarize(lines):
    """Rows keyed on (model, lod): draws, primitives, frames, programs, nodes, origins."""
    models = defaultdict(lambda: dict(draws=0, primitives=0, frames=set(), programs=set(), nodes=set(),
                                      node_handles=set(), unknown_scope=0, origins=[]))
    frames = dict(lines=0, admitted=0, refused_blend=0, opaque=0, alpha=0, refused_routed=0, refused_state=0,
                  refused_variant=0, refused_unknown=0, bind_failures=0, toggled_off_frames=0, programs=0)
    capture_frames = set()
    for line in lines:
        if line.startswith('hull_emission_draw '):
            f = fields(line)
            key = (f.get('model', '?'), f.get('lod', '?'))
            row = models[key]
            row['draws'] += 1
            row['primitives'] += int(f.get('primitives', 0))
            row['frames'].add(int(f['frame']))
            capture_frames.add(int(f['frame']))
            row['programs'].add(int(f['program']))
            if f.get('known') == '1':
                row['nodes'].add((f.get('node'), f.get('node_serial')))
                row['node_handles'].add(int(f.get('node_handle', 0)))
            else:
                row['unknown_scope'] += 1
            if f.get('origin_known') == '1':
                x, y = f['origin_px'].split(',')
                row['origins'].append((float(x), float(y)))
        elif line.startswith('hull_emission_frame '):
            f = fields(line)
            frames['lines'] += 1
            for key in ('admitted', 'refused_blend', 'opaque', 'alpha', 'refused_routed', 'refused_state',
                        'refused_variant', 'refused_unknown', 'bind_failures'):
                frames[key] += int(f.get(key, 0))
            frames['programs'] |= int(f.get('programs', '0'), 16)
            if f.get('toggled') == '0':
                frames['toggled_off_frames'] += 1
    table = []
    for (model, lod), row in sorted(models.items(), key=lambda item: (-item[1]['draws'], item[0])):
        origins = row['origins']
        table.append(dict(model=model, lod=lod, draws=row['draws'], primitives=row['primitives'],
                          frames=len(row['frames']), draws_per_frame=row['draws'] / max(1, len(row['frames'])),
                          programs=sorted(row['programs']),
                          program_hashes=[PROGRAMS[p] for p in sorted(row['programs']) if p < len(PROGRAMS)],
                          nodes=len(row['nodes']), node_handles=sorted(row['node_handles']),
                          unknown_scope=row['unknown_scope'],
                          origin_bounds=[min(o[0] for o in origins), min(o[1] for o in origins),
                                         max(o[0] for o in origins), max(o[1] for o in origins)] if origins else None))
    frames['programs'] = '%03x' % frames['programs']
    frames['program_hashes'] = [PROGRAMS[i] for i in range(len(PROGRAMS)) if int(frames['programs'], 16) >> i & 1]
    return dict(capture_frames=sorted(capture_frames), models=table, frame_totals=frames)


def render(summary):
    out = ['capture frames with hull emitter draws: %d' % len(summary['capture_frames'])]
    t = summary['frame_totals']
    out.append('frame totals (%d lines): admitted=%d refused_blend=%d (opaque=%d alpha=%d) refused_routed=%d refused_state=%d '
               'refused_variant=%d refused_unknown=%d bind_failures=%d toggled_off_frames=%d programs=%s' % (
                   t['lines'], t['admitted'], t['refused_blend'], t['opaque'], t['alpha'], t['refused_routed'], t['refused_state'],
                   t['refused_variant'], t['refused_unknown'], t['bind_failures'], t['toggled_off_frames'], t['programs']))
    out.append('%-10s %-10s %6s %10s %6s %8s %-12s %5s %-16s %s' % ('model', 'lod', 'draws', 'prims', 'frames', 'per_frm', 'programs', 'nodes', 'handles', 'origin_px bounds'))
    for row in summary['models']:
        out.append('%-10s %-10s %6d %10d %6d %8.1f %-12s %5d %-16s %s' % (
            row['model'], row['lod'], row['draws'], row['primitives'], row['frames'], row['draws_per_frame'],
            ','.join(map(str, row['programs'])), row['nodes'], ','.join(map(str, row['node_handles']))[:16],
            'none' if row['origin_bounds'] is None else '%.0f,%.0f..%.0f,%.0f' % tuple(row['origin_bounds'])))
    return '\n'.join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('log', help='session-*.log of a run with --hull-emitters and an F8 capture')
    parser.add_argument('--json', action='store_true', help='machine-readable summary instead of the table')
    args = parser.parse_args(argv)
    with open(args.log, errors='replace') as stream:
        summary = summarize(stream)
    print(json.dumps(summary, indent=1) if args.json else render(summary))
    return 0


if __name__ == '__main__':
    sys.exit(main())
