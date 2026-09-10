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


def summarize(trace, index):
    matches = defaultdict(set)
    for effect in index.get('effects', []):
        for shader in effect['shaders']:
            matches[shader['fnv1a64']].add(effect['path'])
    shaders = {}
    frames = {}
    current = None
    for line in trace.splitlines():
        f = fields(line)
        event = line.partition(' ')[0]
        if event == 'shader':
            shaders[f['id']] = dict(stage=f['kind'], bytes=int(f['bytes']),
                                    effect_candidates=sorted(matches[f['id']]))
        elif event == 'draw':
            current = dict(index=int(f['index']), kind=f['kind'], vs=f['vs'], ps=f['ps'],
                           primitives=int(f['primitives']), targets=[], states={})
            frames.setdefault(f['frame'], {'draws': [], 'complete': False})['draws'].append(current)
        elif event == 'surface' and current is not None and re.fullmatch(r'rt\d', f.get('role', '')):
            current['targets'].append(f)
        elif event == 'state' and current is not None:
            current['states'][f['id']] = int(f['value'])
        elif event == 'frame_end':
            if f['frame'] in frames:
                frames[f['frame']]['complete'] = True
                frames[f['frame']]['present_result'] = f['present']
            current = None
    for frame in frames.values():
        frame['shader_pairs'] = [dict(vs=v, ps=p, draws=n) for (v,p),n in
                                 Counter((d['vs'], d['ps']) for d in frame['draws']).most_common()]
    return dict(shader_count=len(shaders), matched_shader_count=sum(bool(s['effect_candidates']) for s in shaders.values()),
                shaders=shaders, frames=frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.trace.read_text(), json.loads(args.index.read_text()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{result['shader_count']} shaders, {result['matched_shader_count']} matched; "
          f"{len(result['frames'])} captured frames. Wrote {args.output}")


if __name__ == '__main__':
    main()
