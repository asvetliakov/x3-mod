#!/usr/bin/env python3
"""Per-case check counts of run_motion_output.py with the logging tiers against the committed 2026-09-25 counts.

Committed inputs: ../launcher-defaults/motion-cases-2026-09-25.json (229 cases / 346,327 checks, the run after the obsolete-
option removal) and motion-cases-2026-09-26.json here (case -> checks / exit, extracted from the runner's summary with
`extract`). Prints the totals, the cases only before or only after, and every common case whose count differs; exit 1 when a
common case differs, a case exited non-zero, a case is missing, or the new cases are not exactly seam-log-tiers and seam-exit-path.

    python3 verification/results/logging-tiers/compare_motion_counts.py extract verification/results/bottle-X3/motion-output-summary.json
    python3 verification/results/logging-tiers/compare_motion_counts.py
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
BEFORE = HERE.parent / 'launcher-defaults/motion-cases-2026-09-25.json'
AFTER = HERE / 'motion-cases-2026-09-26.json'
NEW = {'seam-log-tiers', 'seam-exit-path'}


def extract(summary_path):
    summary = json.loads(Path(summary_path).read_text())
    cases = {name: {'checks': entry.get('checks'), 'exit': entry.get('exit')} for name, entry in sorted(summary['cases'].items())}
    tiers = summary['cases'].get('seam-log-tiers', {})
    AFTER.write_text(json.dumps({'status': summary.get('status'), 'cases': cases,
                                 'log_tiers': {k: tiers.get(k) for k in ('equivalence', 'always', 'always_with_capture', 'bench', 'bench_log_writer',
                                                                         'debug_log_writer', 'perf_log_writer', 'exception_row')},
                                 'exit_path': summary['cases'].get('seam-exit-path', {}).get('variants')},
                                indent=0, sort_keys=True) + '\n')


def main():
    if len(sys.argv) == 3 and sys.argv[1] == 'extract':
        extract(sys.argv[2])
        return 0
    before, after = json.loads(BEFORE.read_text()), json.loads(AFTER.read_text())
    b = {name: entry['checks'] for name, entry in before['cases'].items()}
    a = {name: entry['checks'] for name, entry in after['cases'].items()}
    changed = {name: [b[name], a[name]] for name in sorted(set(b) & set(a)) if b[name] != a[name]}
    failed = [name for name, entry in after['cases'].items() if entry.get('exit') not in (0, None)]
    result = dict(before=dict(status=before['status'], cases=len(b), checks=sum(b.values())),
                  after=dict(status=after['status'], cases=len(a), checks=sum(a.values()),
                             common_checks=sum(a[name] for name in set(a) & set(b))),
                  only_before=sorted(set(b) - set(a)), only_after={name: a[name] for name in sorted(set(a) - set(b))},
                  changed=changed, failed=failed)
    print(json.dumps(result, indent=1))
    ok = not changed and not failed and not result['only_before'] and set(result['only_after']) == NEW and after['status'] == 'PASS'
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
