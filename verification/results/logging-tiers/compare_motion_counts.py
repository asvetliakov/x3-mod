#!/usr/bin/env python3
"""Per-case check counts of run_motion_output.py with the logging tiers against the committed 2026-09-25 counts.

Committed inputs: ../launcher-defaults/motion-cases-2026-09-25.json (229 cases / 346,327 checks, the run after the obsolete-
option removal), motion-cases-2026-09-26.json here (the logging-tiers suite run, case -> checks / exit, extracted with
`extract`) and motion-cases-hotkeys-2026-09-26.json (the partial rerun after the in-game keys and capture options were
removed, extracted with `extract-hotkeys` from that run's motion-output-partial.json). The current suite is the 2026-09-26
record without the two sun-shadow toggle cases deleted with the keys, each case the hotkeys rerun covered taken from that
rerun. Exit 1 when a common case differs from 2026-09-25, a case exited non-zero, a case is missing other than the two
deleted ones, the new cases are not exactly seam-log-tiers and seam-exit-path, a rerun case differs from its 2026-09-26
count, or the rerun contains a deleted case.

    python3 verification/results/logging-tiers/compare_motion_counts.py extract verification/results/bottle-X3/motion-output-summary.json
    python3 verification/results/logging-tiers/compare_motion_counts.py extract-hotkeys verification/results/bottle-X3/motion-output-partial.json
    python3 verification/results/logging-tiers/compare_motion_counts.py
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
BEFORE = HERE.parent / 'launcher-defaults/motion-cases-2026-09-25.json'
AFTER = HERE / 'motion-cases-2026-09-26.json'
HOTKEYS = HERE / 'motion-cases-hotkeys-2026-09-26.json'
NEW = {'seam-log-tiers', 'seam-exit-path'}
# Deleted with the Ctrl+Shift+F12 sun-shadow A/B (in-game keys removed 2026-09-26, docs/architecture/comparison-hotkeys.md).
DELETED = {'seam-ownership-shadow-replay-cascades-toggle', 'seam-ownership-shadow-replay-toggle-single'}


def cases_of(summary):
    return {name: {'checks': entry.get('checks'), 'exit': entry.get('exit')} for name, entry in sorted(summary['cases'].items())}


def extract(summary_path):
    summary = json.loads(Path(summary_path).read_text())
    tiers = summary['cases'].get('seam-log-tiers', {})
    AFTER.write_text(json.dumps({'status': summary.get('status'), 'cases': cases_of(summary),
                                 'log_tiers': {k: tiers.get(k) for k in ('equivalence', 'always', 'always_with_capture', 'bench', 'bench_log_writer',
                                                                         'debug_log_writer', 'perf_log_writer', 'exception_row')},
                                 'exit_path': summary['cases'].get('seam-exit-path', {}).get('variants')},
                                indent=0, sort_keys=True) + '\n')


def extract_hotkeys(summary_path):
    summary = json.loads(Path(summary_path).read_text())
    HOTKEYS.write_text(json.dumps({'status': summary.get('status'), 'selected_cases': summary.get('selected_cases'),
                                   'binaries': summary.get('binaries'), 'cases': cases_of(summary)}, indent=0, sort_keys=True) + '\n')


def main():
    if len(sys.argv) == 3 and sys.argv[1] == 'extract':
        extract(sys.argv[2])
        return 0
    if len(sys.argv) == 3 and sys.argv[1] == 'extract-hotkeys':
        extract_hotkeys(sys.argv[2])
        return 0
    before, recorded, rerun = (json.loads(p.read_text()) for p in (BEFORE, AFTER, HOTKEYS))
    b = {name: entry['checks'] for name, entry in before['cases'].items()}
    r = {name: entry['checks'] for name, entry in rerun['cases'].items()}
    rerun_changed = {name: [recorded['cases'].get(name, {}).get('checks'), r[name]] for name in sorted(r)
                     if recorded['cases'].get(name, {}).get('checks') != r[name]}
    current = {name: dict(entry) for name, entry in recorded['cases'].items() if name not in DELETED}
    current.update({name: entry for name, entry in rerun['cases'].items()})
    a = {name: entry['checks'] for name, entry in current.items()}
    changed = {name: [b[name], a[name]] for name in sorted(set(b) & set(a)) if b[name] != a[name]}
    failed = [name for name, entry in current.items() if entry.get('exit') not in (0, None)]
    result = dict(before=dict(status=before['status'], cases=len(b), checks=sum(b.values())),
                  after=dict(status=recorded['status'], cases=len(a), checks=sum(a.values()),
                             common_checks=sum(a[name] for name in set(a) & set(b))),
                  rerun=dict(status=rerun['status'], cases=len(r), checks=sum(r.values()), changed_from_recorded=rerun_changed),
                  only_before=sorted(set(b) - set(a)), only_after={name: a[name] for name in sorted(set(a) - set(b))},
                  changed=changed, failed=failed)
    print(json.dumps(result, indent=1))
    ok = (not changed and not failed and set(result['only_before']) == DELETED and set(result['only_after']) == NEW
          and recorded['status'] == 'PASS' and rerun['status'] == 'PARTIAL' and not rerun_changed and not (set(r) & DELETED))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
