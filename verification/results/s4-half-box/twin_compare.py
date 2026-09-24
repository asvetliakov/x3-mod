#!/usr/bin/env python3
"""S4 half-resolution box: the motion-output half twin (`seam-taa-thin-hold-half`, X3M_TAA_BOX_RESOLUTION=half) against its
full-resolution case (`seam-taa-thin-hold-on`) in the committed summary, leaf by leaf (colour hashes included); and the
twin's reference containment line and DLL rows from its retained build directory when present (untracked).

    python3 verification/results/s4-half-box/twin_compare.py"""
import collections
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
cases = json.loads((ROOT / 'verification/results/bottle-X3/motion-output-summary.json').read_text())['cases']


def leaves(value, path=()):
    if isinstance(value, dict):
        for key, item in value.items():
            yield from leaves(item, path + (str(key),))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from leaves(item, path + (str(index),))
    else:
        yield path, value


full, half = dict(leaves(cases['seam-taa-thin-hold-on'])), dict(leaves(cases['seam-taa-thin-hold-half']))
differ = collections.Counter('/'.join(p for p in path if not p.isdigit()) for path in full if path in half and full[path] != half[path])
print(f'leaves full={len(full)} half={len(half)} only_full={len(set(full) - set(half))} only_half={len(set(half) - set(full))} differing={dict(differ)}')
print(f"color_hashes identical={full.get(('color_hashes',)) == half.get(('color_hashes',)) if ('color_hashes',) in full else [k for k in full if k[0] == 'color_hashes'] and all(full[k] == half.get(k) for k in full if k[0] == 'color_hashes')}")
builds = sorted((ROOT / 'verification/probe/build').glob('motion-output-seam-taa-thin-hold-half-*'))
if builds:
    stdout = (builds[-1] / 'fixture-stdout.txt').read_text()
    print(next((l for l in stdout.splitlines() if l.startswith('REFERENCE_BOX_CONTAINMENT ')), 'no containment line'))
    for log in (builds[-1] / 'x3-modern-captures').glob('session-*.log'):
        for line in log.read_text().splitlines():
            if line.startswith('motion_output_taa_box_resolution '):
                print(line)
