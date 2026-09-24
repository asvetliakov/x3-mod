#!/usr/bin/env python3
"""--shadow-alpha-casters, option off: every shadow-replay case of a partial
motion-output run against the committed summary record (HEAD). Compared per
case: checks and the whole `map` record (per frame/cascade covered texels,
disagreements, ambiguous count and the max depth errors as floats; equal
floats mean equal map bytes at every compared texel). The alpha-on DLL twin is
compared with the option-off cascade case of the committed record.
Usage (repository root): python3 verification/results/shadow-alpha-casters/compare_committed.py"""
import json
import subprocess
import sys

committed = json.loads(subprocess.run(['git', 'show', 'HEAD:verification/results/bottle-X3/motion-output-summary.json'],
                                      capture_output=True, text=True, check=True).stdout)['cases']
partial = json.load(open('verification/results/bottle-X3/motion-output-partial.json'))
print(f"partial passed={partial['passed']} status={partial['status']} cases={len(partial['cases'])}")
twin = {'seam-ownership-shadow-replay-cascades-alpha-on': 'seam-ownership-shadow-replay-cascades'}
same = differ = 0
for name, case in sorted(partial['cases'].items()):
    if 'maps' in case or 'func' in case:  # the pass-level and route-level alpha scripts: their own records, no committed twin
        continue
    ref = committed.get(twin.get(name, name))
    if ref is None:
        print(f'{name}: no committed record')
        continue
    checks_equal = case['checks'] - case.get('alpha', {}).get('checks', 0) == ref['checks']
    ok = case.get('map') == ref.get('map') and checks_equal
    same += ok
    differ += not ok
    print(f"{name}: map_equal={case.get('map') == ref.get('map')} has_map={'map' in case} checks={case['checks']}/{ref['checks']}" + (f" (vs {twin[name]})" if name in twin else ''))
print(f'identical={same} differing={differ}')
sys.exit(1 if differ else 0)
