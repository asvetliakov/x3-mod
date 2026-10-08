#!/usr/bin/env python3
"""Single-nozzle equivalence of the plate cap of 72 (docs/architecture/engine-light.md "Plate cap"); adapted from
verification/results/engine-light-per-plate/compare_reports.py (the GPU rows matched by nozzle count and size, the
baseline commit and both reports' SHA-256 recorded).

Compares two raw reports of verification/probe/engine_light_fixture.exe (verification/probe/build/
engine-light-report.txt, untracked, ~16-22 MB each): one built before the change, one after. Per CASE id (one nozzle each)
the SHA-256 of its S rows (every second pixel: base and twin RGB, twin depth, identical flag) and its CASE row; per
PLATE mode the SHA-256 of its P rows (the light-map term) and the PLATE row's light; the RESET row; the GPU rows'
timings and the COST row from the two compact records. Prints one JSON object; equal hashes mean bit-identical
readbacks of the sampled pixels.

    python3 compare_reports.py BEFORE_REPORT AFTER_REPORT BEFORE_RECORD AFTER_RECORD [BASELINE_COMMIT]
"""
import hashlib
import json
import sys
from collections import defaultdict


def digest(path):
    s_rows, p_rows, case_rows, plate_rows, other = defaultdict(hashlib.sha256), defaultdict(hashlib.sha256), {}, {}, {}
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith('S '):
                s_rows[int(line.split(' ', 2)[1])].update(line.encode())
            elif line.startswith('P '):
                p = line.split(' ', 3)
                p_rows[(p[1], p[2])].update(line.encode())
            elif line.startswith('CASE '):
                fields = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
                case_rows[int(fields['id'])] = line.strip()
            elif line.startswith('PLATE '):
                fields = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
                plate_rows[(fields['pair'], fields['mode'])] = fields['light']
            elif line.startswith(('RESET ', 'INVARIANT ')):
                other[line.split()[0] + ' ' + line.split()[1]] = line.strip()
    return ({k: v.hexdigest() for k, v in s_rows.items()}, {k: v.hexdigest() for k, v in p_rows.items()}, case_rows,
            plate_rows, other)


def main():
    before, after = digest(sys.argv[1]), digest(sys.argv[2])
    records = [json.load(open(p)) for p in sys.argv[3:5]]
    out = dict(cases={}, plate_modes={}, other={})
    for cid in sorted(before[2]):
        out['cases'][cid] = dict(samples_identical=before[0].get(cid) == after[0].get(cid),
                                 case_row_identical=before[2][cid] == after[2].get(cid), sha256=after[0].get(cid))
    for key in sorted(before[1]):
        out['plate_modes']['%s-%s' % key] = dict(rows_identical=before[1][key] == after[1].get(key),
                                                 light_identical=before[3][key] == after[3].get(key))
    for key in sorted(before[4]):
        out['other'][key] = before[4][key] == after[4].get(key)
    out['new_cases'] = sorted(set(after[0]) - set(before[0]))
    before_gpu = {(g['nozzles'], g['width']): g for g in records[0]['gpu']}
    out['gpu'] = [dict(nozzles=a['nozzles'], width=a['width'], height=a['height'], after_term_us=round(a['term_us'], 1),
                       before_term_us=round(before_gpu[(a['nozzles'], a['width'])]['term_us'], 1)
                       if (a['nozzles'], a['width']) in before_gpu else None)
                  for a in records[1]['gpu']]
    out['cost'] = {k: dict(before=records[0]['cost'][k], after=records[1]['cost'][k]) for k in ('hit_ns', 'build_us', 'ships_us')}
    out['cost_plates'] = records[1].get('cost_plates')
    out['baseline'] = dict(commit=sys.argv[5] if len(sys.argv) > 5 else None,
                           report_sha256=hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest(),
                           executable_sha256=records[0].get('executable_sha256'))
    out['after'] = dict(report_sha256=hashlib.sha256(open(sys.argv[2], 'rb').read()).hexdigest(),
                        executable_sha256=records[1].get('executable_sha256'))
    out['wine_env'] = [r.get('wine_env') for r in records]
    single = [m for m in out['plate_modes'] if m.split('-')[1] in ('twin', 'gain1', 'nolight', 'nogain')]
    out['single_nozzle_identical'] = (all(c['samples_identical'] and c['case_row_identical'] for c in out['cases'].values()) and
                                      all(out['plate_modes'][m]['rows_identical'] and out['plate_modes'][m]['light_identical']
                                          for m in single) and all(out['other'].values()))
    print(json.dumps(out, indent=1, sort_keys=True))


if __name__ == '__main__':
    main()
