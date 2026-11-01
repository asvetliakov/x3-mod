#!/usr/bin/env python3
"""Baseline for the next FL flight: gate-3 (pair) refusals in the FL run logs before the FL motion rows.

Reads `sun_shadow_lane_refusals` samples (untracked / pair counts) and the
`sun_shadow_lane_writer` rows of each log, and counts how many refused
writers are pairs of the FL extension rows (motion-output-profiles-fl.json).
After the rows are installed, the pair refusals of those writers should be
gone. Writes gate3-baseline.json beside this script (counts only).

usage: gate3_baseline.py --log NAME=PATH...
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
WRITER = re.compile(r'^sun_shadow_lane_writer .* vs=([0-9a-f]{16}) ps=([0-9a-f]{16}) reason=(\w+) gate=(\d+)', re.M)
REFUSALS = re.compile(r'^sun_shadow_lane_refusals .*? untracked=(\d+) .*? unregistered=(\d+) pair=(\d+)', re.M)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--log', action='append', required=True, metavar='NAME=PATH')
    arguments = parser.parse_args()
    rows = {(p['vs'], p['ps']) for p in json.loads((HERE / 'motion-output-profiles-fl.json').read_text())['pairs']}
    result = {}
    for item in arguments.log:
        name, _, path = item.partition('=')
        text = Path(path).read_text(errors='replace')
        writers = Counter(('fl_row' if (vs, ps) in rows else 'other', reason, int(gate))
                          for vs, ps, reason, gate in WRITER.findall(text))
        samples = [tuple(map(int, sample)) for sample in REFUSALS.findall(text)]
        result[name] = {
            'path': path,
            'writer_rows': [{'pair': k, 'reason': r, 'gate': g, 'rows': c} for (k, r, g), c in sorted(writers.items())],
            'refusal_samples': len(samples),
            'samples_all_untracked_are_pair': all(u == p for u, _, p in samples),
            'max_untracked': max((u for u, _, _ in samples), default=0),
            'max_pair': max((p for _, _, p in samples), default=0),
            'max_unregistered': max((r for _, r, _ in samples), default=0),
        }
    (HERE / 'gate3-baseline.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
