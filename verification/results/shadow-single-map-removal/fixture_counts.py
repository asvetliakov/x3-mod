#!/usr/bin/env python3
"""Single shadow map removed (2026-09-25): per-case fixture checks before and after.

Before: the committed full motion-output summary at BEFORE (verification/results/bottle-X3/
motion-output-summary.json, the Run 87 suite: 234 cases; cross-checked against the Run 87 candidate's
copy when /tmp/x3-run87-candidate/motion-summary-run87.json is present). After: this checkout's
partial summary of the shadow subset (verification/results/bottle-X3/motion-output-partial.json).
Prints one row per shadow case (kept / moved / deleted / added, checks before -> after) and writes
fixture_counts.json beside this script. Every kept or moved case must pass with checks >= before.
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BEFORE = sys.argv[1] if len(sys.argv) > 1 else 'e17fd2ca'
RUN87 = Path('/tmp/x3-run87-candidate/motion-summary-run87.json')
PREFIXES = ('seam-ownership-shadow-', 'seam-ownership-taa-shadow-replay-', 'sun-shadow-apply', 'shadow-alpha-casters')
MOVED = {'seam-ownership-shadow-replay-on', 'seam-ownership-taa-shadow-replay-on', 'seam-ownership-shadow-replay-casters-2',
         'seam-ownership-shadow-replay-casters-8', 'seam-ownership-shadow-replay-casters-20', 'seam-ownership-shadow-replay-sun-programs',
         'seam-ownership-shadow-replay-toggle-single', 'seam-ownership-shadow-replay-wide', 'seam-ownership-shadow-replay-far-refused',
         'seam-ownership-shadow-alpha-route', 'seam-ownership-shadow-alpha-route-less', 'shadow-alpha-casters'}
EXTENDED = {'sun-shadow-apply-cascades', 'sun-shadow-apply-cascades-5', 'sun-shadow-apply-cascades-5-faces', 'sun-shadow-apply-cascades-slope',
            'sun-shadow-apply-cascades-5-slope', 'sun-shadow-apply-cascades-5-faces-slope'}  # the single script's skip paths moved in


def shadow(name):
    return name.startswith(PREFIXES) or name == 'seam-ownership-taa-camera-candidates-on'


def main():
    before_text = subprocess.run(['git', 'show', f'{BEFORE}:verification/results/bottle-X3/motion-output-summary.json'], cwd=ROOT,
                                 capture_output=True, text=True, check=True).stdout
    before = {n: c for n, c in json.loads(before_text)['cases'].items() if shadow(n)}
    run87_equal = None
    if RUN87.is_file():
        run87 = {n: c.get('checks') for n, c in json.loads(RUN87.read_text())['cases'].items() if shadow(n)}
        run87_equal = run87 == {n: c.get('checks') for n, c in before.items()}
    partial = json.loads((ROOT / 'verification/results/bottle-X3/motion-output-partial.json').read_text())
    after = {n: c for n, c in partial['cases'].items() if shadow(n)}
    rows, failures = [], []
    for name in sorted(set(before) | set(after)):
        b, a = before.get(name, {}).get('checks'), after.get(name, {}).get('checks')
        status = 'deleted' if a is None else 'added' if b is None else 'moved' if name in MOVED else 'extended' if name in EXTENDED else 'kept'
        rows.append({'case': name, 'status': status, 'before': b, 'after': a})
        if status in ('kept', 'moved', 'extended') and not (a is not None and b is not None and a >= b):
            failures.append(name)
    totals = {s: {'cases': sum(r['status'] == s for r in rows), 'before': sum(r['before'] or 0 for r in rows if r['status'] == s),
                  'after': sum(r['after'] or 0 for r in rows if r['status'] == s)} for s in ('kept', 'moved', 'extended', 'added', 'deleted')}
    report = {'before': BEFORE, 'run87_copy_equal': run87_equal, 'partial_status': partial.get('status'),
              'case_exits_nonzero': sorted(n for n, c in after.items() if c.get('exit') != 0),
              'rows': rows, 'totals': totals, 'failures': failures,
              'before_total': sum(r['before'] or 0 for r in rows), 'after_total': sum(r['after'] or 0 for r in rows)}
    (HERE / 'fixture_counts.json').write_text(json.dumps(report, indent=1) + '\n')
    for r in rows:
        print(f"{r['status']:8} {r['case']:58} {r['before']!s:>6} -> {r['after']!s:>6}")
    print('totals', totals, 'before', report['before_total'], 'after', report['after_total'], 'run87_copy_equal', run87_equal,
          'partial', report['partial_status'], 'nonzero_exits', report['case_exits_nonzero'], 'failures', failures)
    if failures or report['partial_status'] not in ('PASS', 'PARTIAL') or report['case_exits_nonzero']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
