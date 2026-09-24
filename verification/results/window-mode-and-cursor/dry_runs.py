#!/usr/bin/env python3
"""The four launcher dry runs of the window/cursor options against the installed bottle
(tools/manage.py launch --dry-run; never a launch): prints, per run, the exit code and the
X3M_WINDOW_* / X3M_CURSOR_REASSERT / X3M_TELEMETRY variables the launch would carry, and
whether the command equals the default modded --taa run's. Ledger:
docs/verification/window-and-cursor.md."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TAA = ['--direct', '--motion-output', '--taa', '--object-trace', '--object-lifetime', '--ownership']
RUNS = {
    'modded_taa_default': TAA,
    'monitor_rect_off': TAA + ['--window-monitor-rect', 'off'],
    'trace_and_reassert': TAA + ['--telemetry', '--window-trace', '--cursor-reassert'],
    'vanilla': ['--direct', '--vanilla'],
}
NAMES = ('X3M_WINDOW_MONITOR_RECT', 'X3M_WINDOW_MONITOR_RECT_DEFAULT', 'X3M_WINDOW_TRACE', 'X3M_CURSOR_REASSERT', 'X3M_TELEMETRY')


def main():
    reference = None
    for label, args in RUNS.items():
        run = subprocess.run([sys.executable, str(ROOT / 'tools/manage.py'), 'launch', '--dry-run', *args], capture_output=True, text=True, cwd=ROOT)
        if run.returncode:
            print(json.dumps({'run': label, 'exit': run.returncode, 'error': run.stderr.strip().splitlines()[-1:]}))
            continue
        data = json.loads(run.stdout)
        reference = reference or data['command']
        print(json.dumps({'run': label, 'exit': 0, 'env': {k: data['env'][k] for k in NAMES if k in data['env']},
                          'command_equals_default': data['command'] == reference}))


if __name__ == '__main__':
    main()
