#!/usr/bin/env python3
"""Per-case check counts of run_motion_output.py before and after the obsolete-option removal of 2026-09-25.

Committed inputs (this directory): motion-cases-run89.json (the Run89 qualification run, case -> checks / exit, extracted
from its retained summary /tmp/x3-run89-candidate/motion-summary-run89.json) and motion-cases-2026-09-25.json (the run
after the removal). The Run89 file is cross-checked against the totals recorded in
verification/results/run89-candidate-qualification.json (motion_output: 233 cases, 346,838 checks).

    python3 verification/results/launcher-defaults/compare_motion_counts_2026-09-25.py
    python3 verification/results/launcher-defaults/compare_motion_counts_2026-09-25.py extract SUMMARY.json OUT.json

Prints the totals, the cases only before (with their counts), the cases only after, and every common case whose count
differs; exit 1 when a common case differs, a case exited non-zero, or the Run89 file disagrees with the record.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BEFORE, AFTER = HERE / 'motion-cases-run89.json', HERE / 'motion-cases-2026-09-25.json'


def extract(summary_path, out_path):
    summary = json.loads(Path(summary_path).read_text())
    cases = {name: {'checks': entry.get('checks'), 'exit': entry.get('exit')} for name, entry in sorted(summary['cases'].items())}
    Path(out_path).write_text(json.dumps({'status': summary.get('status'), 'cases': cases}, indent=0, sort_keys=True) + '\n')


def main():
    if len(sys.argv) == 4 and sys.argv[1] == 'extract':
        extract(sys.argv[2], sys.argv[3])
        return 0
    before, after = json.loads(BEFORE.read_text()), json.loads(AFTER.read_text())
    b = {name: entry['checks'] for name, entry in before['cases'].items()}
    a = {name: entry['checks'] for name, entry in after['cases'].items()}
    record = json.loads((ROOT / 'verification/results/run89-candidate-qualification.json').read_text())['motion_output']
    record_totals = (record['cases'], record['per_case_vs_run88']['checks'][1])
    changed = {name: [b[name], a[name]] for name in sorted(set(b) & set(a)) if b[name] != a[name]}
    failed = [name for name, entry in list(before['cases'].items()) + list(after['cases'].items()) if entry.get('exit') not in (0, None)]
    result = dict(before=dict(status=before['status'], cases=len(b), checks=sum(b.values()), record_cases_checks=list(record_totals)),
                  after=dict(status=after['status'], cases=len(a), checks=sum(a.values())),
                  only_before={name: b[name] for name in sorted(set(b) - set(a))}, only_after={name: a[name] for name in sorted(set(a) - set(b))},
                  changed=changed, failed=failed)
    print(json.dumps(result, indent=1))
    agrees = (len(b), sum(b.values())) == record_totals and before['status'] == after['status'] == 'PASS'
    return 0 if agrees and not changed and not failed else 1


if __name__ == '__main__':
    sys.exit(main())
