#!/usr/bin/env python3
"""AP census before/after the shared mount helper, key by key.

Exports `tools/analysis` at the pre-change commit (default c87d81b8, the base
of FL shader coverage 4a) into a temporary directory, runs its
`shader_coverage_offline.py` and the working tree's over the same stock AP
installation, and compares every top-level key except `tool_sha256`:
  same_registry: both read only motion_output_profiles_inc.h -> must be identical;
  compiled_table: the current default (AP + FL includes) -> the differing keys
  are listed (only the registry: the 34 inert FL rows).
Writes ap-census-regression.json beside this script (key names and counts only).

usage: ap_census_regression.py [--ap DIR] [--base COMMIT]
"""
import argparse
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
AP_HEADER = ROOT / 'src/renderer/motion_output_profiles_inc.h'


def census(tool, game, output, headers):
    # Same registry inputs for both trees (the exported tree has no results or src).
    command = [sys.executable, str(tool), '--game', str(game), '--output', str(output),
               '--profiles', str(ROOT / 'verification/results/motion-output-profiles.json'),
               '--prepass-header', str(ROOT / 'src/renderer/depth_prepass_profiles.h')]
    for header in headers:
        command += ['--header', str(header)]
    subprocess.run(command, check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
    return json.loads(output.read_text())


def compare(before, after):
    keys = sorted((set(before) | set(after)) - {'tool_sha256'})
    return {'keys_compared': len(keys), 'differing_keys': [k for k in keys if before.get(k) != after.get(k)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--ap', type=Path, default=Path('/mnt/c/X3AP-orig'))
    parser.add_argument('--base', default='c87d81b8')
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='x3-ap-census-') as temp:
        temp = Path(temp)
        archive = subprocess.run(['git', 'archive', arguments.base, 'tools/analysis'], cwd=ROOT, check=True,
                                 capture_output=True).stdout
        tarfile.open(fileobj=io.BytesIO(archive)).extractall(temp / 'base', filter='data')
        old_tool = temp / 'base/tools/analysis/shader_coverage_offline.py'
        new_tool = ROOT / 'tools/analysis/shader_coverage_offline.py'
        before = census(old_tool, arguments.ap, temp / 'before.json', [AP_HEADER])
        same = census(new_tool, arguments.ap, temp / 'same.json', [AP_HEADER])
        compiled = census(new_tool, arguments.ap, temp / 'compiled.json', [])
    same_registry = compare(before, same)
    compiled_table = compare(before, compiled)
    absent = compiled['registry']['registered_rows_absent_from_archive']
    result = {
        'schema': 1,
        'producer': str(Path(__file__).relative_to(ROOT)),
        'game': str(arguments.ap), 'base_commit': arguments.base,
        'pre_change_tool_sha256': before['tool_sha256'], 'post_change_tool_sha256': same['tool_sha256'],
        'same_registry': {**same_registry, 'identical': not same_registry['differing_keys']},
        'compiled_table': {**compiled_table,
                           'profile_rows': [before['registry']['profile_rows'], compiled['registry']['profile_rows']],
                           'rows_absent_from_ap_archive': len(absent)},
        'pre_change': {'archive': before['archive'], 'pair_count': before['pair_count'],
                       'coverage_counts': before['coverage_counts']},
    }
    (HERE / 'ap-census-regression.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result, indent=1))
    ok = result['same_registry']['identical'] and compiled_table['differing_keys'] == ['registry']
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
