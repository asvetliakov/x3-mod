#!/usr/bin/env python3
"""Summarize proxy captures and match exact shader hashes to local effect metadata.

An exact hash match identifies candidate effects, not a unique material or pass.
Multiple compiled effects can embed identical shaders. Never infer HUD separation
or HDR precision from an effect name alone; inspect target/order/state evidence.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def summarize(trace, index, include_floats=False):
    matches = defaultdict(set)
    for effect in index.get('effects', []):
        for shader in effect['shaders']:
            matches[shader['fnv1a64']].add(effect['path'])
    shaders = {}
    frames = {}
    current = None
    boundary = None
    resources = {}
    def frame_key(f):
        return f"{f['device']}:{f['frame']}" if 'device' in f else f['frame']
    for line in trace.splitlines():
        f = fields(line)
        event = line.partition(' ')[0]
        if event == 'shader':
            shaders[f['id']] = dict(stage=f['kind'], bytes=int(f['bytes']),
                                    effect_candidates=sorted(matches[f['id']]))
        elif event == 'resource':
            resources[f['identity']] = f
        elif event == 'frame_begin':
            frames.setdefault(frame_key(f), {'draws': [], 'complete': False})
            current = None
            boundary = None
        elif event == 'capture_event':
            boundary = dict(f, details=[])
            frames.setdefault(frame_key(f), {'draws': [], 'complete': False}).setdefault('events', []).append(boundary)
            current = None
        elif boundary is not None and boundary['op'] != 'draw_begin' and event in (
                'clear', 'clear_rect', 'clear_rects', 'set_rt', 'set_depth', 'stretch_rect', 'stretch_source_rect',
                'stretch_dest_rect', 'surface'):
            boundary['details'].append(dict(event=event, **f))
        elif event == 'draw':
            current = dict(index=int(f['index']), kind=f['kind'], vs=f['vs'], ps=f['ps'],
                           primitives=int(f['primitives']), topology=int(f['topology']), targets=[], states={})
            if 'device' in f:
                current['device'] = int(f['device'])
            frames.setdefault(frame_key(f), {'draws': [], 'complete': False})['draws'].append(current)
        elif event == 'surface' and current is not None and re.fullmatch(r'rt\d', f.get('role', '')):
            current['targets'].append(f)
        elif event == 'state' and current is not None:
            current['states'][f['id']] = int(f['value'])
        elif event in ('geometry','indices','draw_args','viewport','draw_result') and current is not None:
            current[event] = f
        elif event in ('stream','vertex_buffer','index_buffer','texture','texture_desc','sampler','vertex_element') and current is not None:
            current.setdefault(event, []).append(f)
        elif event == 'surface' and current is not None and f.get('role') == 'depth':
            current['depth'] = f
        elif event == 'constants' and current is not None:
            current.setdefault('constant_status', {}).setdefault(f['kind'], {})[f.get('type','f')] = f
        elif event == 'constant' and current is not None:
            typ = f.get('type','f')
            if typ != 'f' or include_floats:
                namespace = current.setdefault('constants', {}).setdefault(f['kind'], {}).setdefault(typ,{})
                # Float bit strings preserve NaN/signed-zero; integer and BOOL
                # namespaces contain raw signed numeric values, including zeros.
                namespace[f['reg']] = f['bits'] if typ == 'f' else [int(v) for v in f['values'].split(',')]
        elif event == 'frame_end':
            key = frame_key(f)
            if key in frames:
                frame = frames[key]
                frame['complete'] = True
                frame['present_result'] = f['present']
                frame['reported_draws'] = int(f['draws'])
                frame['draw_count_matches'] = len(frame['draws']) == int(f['draws'])
            current = None
            boundary = None
    for frame in frames.values():
        if 'events' in frame:
            sequence = [int(e['seq']) for e in frame['events']]
            frame['event_sequence_contiguous'] = sequence == list(range(1, len(sequence)+1))
        frame['shader_pairs'] = [dict(vs=v, ps=p, draws=n) for (v,p),n in
                                 Counter((d['vs'], d['ps']) for d in frame['draws']).most_common()]
    return dict(shader_count=len(shaders), matched_shader_count=sum(bool(s['effect_candidates']) for s in shaders.values()),
                shaders=shaders, frames=frames, resources=resources)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--include-floats', action='store_true', help='Keep sparse float constants in addition to integer/BOOL state')
    args = parser.parse_args()
    result = summarize(args.trace.read_text(), json.loads(args.index.read_text()), args.include_floats)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{result['shader_count']} shaders, {result['matched_shader_count']} matched; "
          f"{len(result['frames'])} captured frames. Wrote {args.output}")


if __name__ == '__main__':
    main()
