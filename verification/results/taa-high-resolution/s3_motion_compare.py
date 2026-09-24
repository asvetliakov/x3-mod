#!/usr/bin/env python3
"""S3: the motion-output summary against the committed one (HEAD, or --against REV), every differing stable field.

Same volatile-field rule as verification/results/sky-history-default/compare_motion_output.py (paths, binary hashes and
timings set aside), but lists every differing field path grouped by its path with the case names stripped, with the
number of cases and up to three old -> new examples, so the resolve-dependent fields can be named exhaustively.
"""
import argparse
from collections import OrderedDict
import json
import re
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
SUMMARY = 'verification/results/bottle-X3/motion-output-summary.json'
VOLATILE = {'directory', 'dll_sha256', 'exe_sha256', 'trace_sha256'}


def volatile(key):
    return key in VOLATILE or key == 'us' or '_us' in key or '_ns' in key or key.endswith(('_ms', '_seconds')) or 'timing' in key or 'sha256' in key


def leaves(a, b, prefix=''):
    if isinstance(a, dict) and isinstance(b, dict):
        out = []
        for k in sorted(set(a) | set(b)):
            if not volatile(k):
                out += leaves(a.get(k), b.get(k), prefix + '/' + k)
        return out
    return [] if a == b else [(prefix, a, b)]


def short(value):
    text = json.dumps(value)
    return text if len(text) <= 60 else text[:57] + '...'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--against', default='HEAD')
    args = parser.parse_args()
    old = json.loads(subprocess.run(['git', 'show', f'{args.against}:{SUMMARY}'], cwd=ROOT, capture_output=True, text=True, check=True).stdout)
    new = json.loads((ROOT / SUMMARY).read_text())
    oc, nc = old['cases'], new['cases']
    print(f'passed old={old["passed"]} new={new["passed"]}')
    print(f'cases old={len(oc)} new={len(nc)} missing={sorted(set(oc) - set(nc))} added={sorted(set(nc) - set(oc))}')
    print(f'checks old={sum(c.get("checks", 0) for c in oc.values())} new={sum(c.get("checks", 0) for c in nc.values())}')
    # The 16-tap case against the committed seam-taa-on (pre-S3 when --against is the pre-S3 commit): the resolved colour
    # and its checks should match; the case is new, so it is left out of the per-case diff below.
    if 'seam-taa-taps16' in nc and 'seam-taa-on' in oc:
        t, o = nc['seam-taa-taps16'], oc['seam-taa-on']
        same = {k: t.get(k) == o.get(k) for k in ('color_hashes', 'color_hashes_before_boundary', 'checks', 'taa_changed_pixels')}
        print(f'seam-taa-taps16 (new) vs {args.against} seam-taa-on: ' + ' '.join(f'{k}_identical={int(v)}' for k, v in same.items()))
    groups, cases = OrderedDict(), set()
    for name in sorted(set(oc) & set(nc)):
        for path, a, b in leaves(oc[name], nc[name]):
            cases.add(name)
            groups.setdefault(path, []).append((name, a, b))
    # Top-level blocks outside `cases` (twins, bench, sources): recursed like the cases; provenance blocks (source and
    # binary hashes) are listed by name only.
    provenance = {'sources_before_build', 'sources_after_run', 'binaries'}
    top = [p for k in sorted(set(old) | set(new)) if k not in {'cases'} | provenance and not volatile(k)
           for p in leaves(old.get(k), new.get(k), '/' + k)]
    print(f'cases with differing stable fields: {len(cases)}; field paths: {len(groups)}; top-level paths differing: {len(top)}')
    collapsed = OrderedDict()
    for path, entries in groups.items():
        collapsed.setdefault(re.sub(r'/\d+', '/#', path), set()).update(n for n, _, _ in entries)
    for path, names in collapsed.items():
        print(f'  field {path} cases={len(names)}')
    for path, entries in groups.items():
        examples = '; '.join(f'{n}: {short(a)} -> {short(b)}' for n, a, b in entries[:3])
        print(f'  {path} cases={len(entries)} e.g. {examples}')
    for path, a, b in top:
        print(f'  top-level {path}: {short(a)} -> {short(b)}')
    print('  provenance blocks differing: ' + ' '.join(k for k in sorted(provenance) if old.get(k) != new.get(k)))
    print(f'differing cases ({len(cases)}): ' + ' '.join(sorted(cases)))
    print('differing cases without "taa" in the name: ' + (' '.join(sorted(n for n in cases if 'taa' not in n)) or 'none'))
    return 0 if new['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
