#!/usr/bin/env python3
"""Offline pair census of FL and stock AP over the compiled motion table (AP + FL includes).

* AP regression: the mount-dependent part of the census of `--ap` (effective
  effects, passes, every (vs, ps) pair with its models, lengths, state classes
  and pass count) must equal the tracked AP census
  `verification/results/shader-coverage-offline.json`; the registry part may
  differ only by rows whose programs AP never creates (the FL rows, inert).
* FL: coverage counts of `--fl`, and every FL-only SM3 pair registered.
Writes census-summary.json beside this script (counts and ids only).

usage: census.py [--fl DIR] [--ap DIR]
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from shader_coverage_offline import (archive_pairs, census, prepass_vertices,  # noqa: E402
                                     registered_pairs, rewriter_verdicts)

AP_HEADER = ROOT / 'src/renderer/motion_output_profiles_inc.h'
FL_HEADER = ROOT / 'src/renderer/motion_output_profiles_fl_inc.h'
TRACKED = ROOT / 'verification/results/shader-coverage-offline.json'
PAIR_FIELDS = ('vs', 'ps', 'vs_model', 'ps_model', 'vs_dwords', 'ps_dwords', 'classes', 'pass_occurrences',
               'depth_candidate', 'basenames', 'techniques', 'pass_names', 'profile_directories',
               'toggle_directories', 'catalogues')


def run(game, rows):
    verdicts = rewriter_verdicts(json.loads((ROOT / 'verification/results/motion-output-profiles.json').read_text()))
    return census(archive_pairs(game), rows, prepass_vertices(ROOT / 'src/renderer/depth_prepass_profiles.h'),
                  verdicts, [])


def mount_part(result):
    return {'archive': result['archive'], 'pair_count': result['pair_count'],
            'class_counts': result['class_counts'],
            'pairs': sorted(json.dumps({k: p[k] for k in PAIR_FIELDS}, sort_keys=True) for p in result['pairs'])}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--fl', type=Path, default=Path('/mnt/c/X3FL'))
    parser.add_argument('--ap', type=Path, default=Path('/mnt/c/X3AP-orig'))
    arguments = parser.parse_args()
    ap_rows, fl_rows = registered_pairs(AP_HEADER), registered_pairs(FL_HEADER)
    rows = ap_rows + fl_rows
    ap = run(arguments.ap, rows)
    fl = run(arguments.fl, rows)
    tracked = json.loads(TRACKED.read_text())
    ap_pairs = {(p['vs'], p['ps']) for p in ap['pairs']}
    fl_only = [p for p in fl['pairs'] if (p['vs'], p['ps']) not in ap_pairs]
    absent_ap = sorted(map(tuple, ap['registry']['registered_rows_absent_from_archive']))
    summary = {
        'schema': 1,
        'producer': str(Path(__file__).relative_to(ROOT)),
        'producer_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'table_rows': {'ap_include': len(ap_rows), 'fl_include': len(fl_rows), 'compiled': len(rows)},
        'ap_regression': {
            'game': str(arguments.ap),
            'mount_dependent_part_equals_tracked_census': mount_part(ap) == mount_part(tracked),
            'archive': ap['archive'], 'pair_count': ap['pair_count'],
            'coverage_counts': ap['coverage_counts'],
            'registered_rows_absent_from_archive': len(absent_ap),
            'absent_rows_are_exactly_the_fl_rows': absent_ap == sorted(fl_rows),
        },
        'fl': {
            'game': str(arguments.fl),
            'archive': fl['archive'], 'pair_count': fl['pair_count'],
            'coverage_counts': fl['coverage_counts'],
            'coverage_by_model': fl['coverage_by_model'],
            'registered_rows_absent_from_archive': len(fl['registry']['registered_rows_absent_from_archive']),
            'fl_only_pairs': len(fl_only),
            'fl_only_pairs_by_coverage': dict(sorted(Counter(p['coverage'] for p in fl_only).items())),
            'fl_only_pairs_by_model': dict(sorted(Counter('%s/%s' % (p['vs_model'], p['ps_model'])
                                                          for p in fl_only).items())),
            'fl_only_depth_candidates': sum(p['depth_candidate'] for p in fl_only),
        },
    }
    (HERE / 'census-summary.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(json.dumps(summary, indent=1))
    ok = (summary['ap_regression']['mount_dependent_part_equals_tracked_census']
          and summary['ap_regression']['absent_rows_are_exactly_the_fl_rows']
          and summary['fl']['fl_only_pairs_by_coverage'] == {'registered': len(fl_rows)})
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
