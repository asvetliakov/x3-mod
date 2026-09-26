#!/usr/bin/env python3
"""Settings file (docs/architecture/config-file.md, steps 1+2): run_motion_output.py per-case check counts against the
committed suite.

The committed suite is ../logging-tiers/motion-cases-2026-09-26.json overlaid with motion-cases-hotkeys-2026-09-26.json
(the two sun-shadow toggle cases deleted with the in-game keys dropped): 229 cases / 345,859 checks. The run under test is
extracted from verification/results/bottle-X3/motion-output-summary.json into motion-cases-config-2026-09-26.json here
(`extract`, case -> checks / exit, plus the seam-config-file detail). Exit 1 when a committed case is missing, changed
its count or exited non-zero, when the new cases are not exactly seam-config-file, or when the run's status is not PASS.

    python3 verification/results/config-file/compare_motion_counts.py extract verification/results/bottle-X3/motion-output-summary.json
    python3 verification/results/config-file/compare_motion_counts.py
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TIERS = HERE.parent / 'logging-tiers'
RECORDED, HOTKEYS = TIERS / 'motion-cases-2026-09-26.json', TIERS / 'motion-cases-hotkeys-2026-09-26.json'
RUN = HERE / 'motion-cases-config-2026-09-26.json'
DELETED = {'seam-ownership-shadow-replay-cascades-toggle', 'seam-ownership-shadow-replay-toggle-single'}
NEW = {'seam-config-file'}


def committed():
    recorded, rerun = (json.loads(p.read_text()) for p in (RECORDED, HOTKEYS))
    cases = {name: entry for name, entry in recorded['cases'].items() if name not in DELETED}
    cases.update(rerun['cases'])
    return cases


def extract(summary_path):
    summary = json.loads(Path(summary_path).read_text())
    cases = {name: {'checks': entry.get('checks'), 'exit': entry.get('exit')} for name, entry in sorted(summary['cases'].items())}
    config = summary['cases'].get('seam-config-file', {})
    detail = {'us': config.get('us'),
              'runs': {how: {k: run.get(k) for k in ('config_open', 'config_key', 'config_file', 'frame_end_stride_mode', 'mode')}
                       for how, run in (config.get('runs') or {}).items()}}
    RUN.write_text(json.dumps({'status': summary.get('status'), 'binaries': summary.get('binaries'), 'cases': cases,
                               'seam_config_file': detail}, indent=0, sort_keys=True) + '\n')


def main():
    if len(sys.argv) == 3 and sys.argv[1] == 'extract':
        extract(sys.argv[2])
        return 0
    base = {name: entry['checks'] for name, entry in committed().items()}
    run = json.loads(RUN.read_text())
    now = {name: entry['checks'] for name, entry in run['cases'].items()}
    changed = {name: [base[name], now[name]] for name in sorted(set(base) & set(now)) if base[name] != now[name]}
    failed = sorted(name for name, entry in run['cases'].items() if entry.get('exit') not in (0, None))
    result = {'committed': {'cases': len(base), 'checks': sum(base.values())},
              'run': {'status': run['status'], 'cases': len(now), 'checks': sum(v or 0 for v in now.values()),
                      'common_checks': sum(now[n] for n in set(now) & set(base))},
              'missing': sorted(set(base) - set(now)), 'new': {n: now[n] for n in sorted(set(now) - set(base))},
              'changed': changed, 'failed': failed, 'config_file_us': run['seam_config_file']['us']}
    print(json.dumps(result, indent=1))
    ok = run['status'] == 'PASS' and not changed and not failed and not result['missing'] and set(result['new']) == NEW
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
