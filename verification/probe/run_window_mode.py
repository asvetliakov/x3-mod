#!/usr/bin/env python3
"""--window-monitor-rect on the bottle's display driver: one Wine run of the CMake-built
fixture, a compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_window_mode.py --exe <build>/window_mode_fixture.exe
The fixture (verification/probe/window_mode_fixture.cpp with the production
src/proxy/window_mode.cpp and window_mode_core.h) creates the game's WS_POPUP window at
the work-area origin, moves it with the production predicate and asserts the monitor
rectangle, the noops and the refusals. It shows a monitor-sized dark window for about
two seconds. This runner never builds it; it parses the GEOMETRY / PLACED / CHECK /
SCREEN / RESULT lines and writes <results>/window-mode.json beside the raw
<results>/window-mode.txt. The SCREEN line (BitBlt of the desktop's top 40 rows) is a
soft record, never asserted. docs/architecture/window-mode-and-cursor-fix.md section 4;
ledger docs/verification/window-and-cursor.md. Never launches the game.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / 'build/window_mode_fixture.exe'
SOURCES = ('src/proxy/window_mode_core.h', 'src/proxy/window_mode.h', 'src/proxy/window_mode.cpp',
           'verification/probe/window_mode_fixture.cpp', 'verification/probe/run_window_mode.py')
EXPECTED_CHECKS = 19  # variable/row 2, class/window 2, placement 1, last error 1, move 1, stays/client 2, second call 1, at-monitor 1, spare 1, refusals 6, option off 1


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        out[key] = int(value) if re.fullmatch(r'-?\d+', value) else value
    return out


def parse(text):
    """The fixture's report as one dictionary (also the host test's subject)."""
    report = {'checks': [], 'geometry': None, 'placed': None, 'screen': None, 'result': None, 'rows': []}
    for line in text.splitlines():
        if line.startswith('CHECK '):
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif line.startswith('GEOMETRY '):
            report['geometry'] = fields(line)
        elif line.startswith('PLACED '):
            report['placed'] = fields(line)
        elif line.startswith('SCREEN '):
            report['screen'] = fields(line)
        elif line.startswith('LOG window_mode '):
            report['rows'].append({k: v for k, v in fields(line).items() if k in ('phase', 'before', 'after', 'action', 'reason', 'result')})
        elif line.startswith('RESULT '):
            report['result'] = dict(fields(line), verdict=line.split()[-1])
    report['check_count'] = len(report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def accept(report):
    """Raises AssertionError naming the first violated acceptance term."""
    result = report['result']
    assert result and result['verdict'] == 'PASS' and not report['failed_checks'], report['failed_checks']
    assert result['checks'] == report['check_count'] == EXPECTED_CHECKS, (result, report['check_count'])
    assert report['geometry'] is not None, 'no GEOMETRY line'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE, help='The CMake-built window_mode_fixture.exe (default build/window_mode_fixture.exe)')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    exe = args.exe.resolve()
    if not exe.is_file():
        sys.exit(f'fixture not built: {exe} (cmake --build <dir> --target window_mode_fixture)')
    results = bottle.results_dir(ROOT)
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'executable': str(exe), 'executable_sha256': sha(exe), 'expected_checks': EXPECTED_CHECKS}
    out_path = results / 'window-mode.json'
    try:
        command = [bottle.WINE, *bottle.wine_args(), '--workdir', str(exe.parent), str(exe)]
        record['command'] = command
        started = time.time()
        run = subprocess.run(command, capture_output=True, text=True, timeout=300)
        record['elapsed_s'] = round(time.time() - started, 1)
        record['exit_code'] = run.returncode
        text_path = results / 'window-mode.txt'
        text_path.write_text(run.stdout + ('\n--- stderr (tail) ---\n' + run.stderr[-4000:] if run.stderr else ''))
        record['report_sha256'] = sha(text_path)
        record['report'] = parse(run.stdout)
        assert run.returncode == 0, run.stdout[-2000:]
        accept(record['report'])
        assert record['sources'] == {s: sha(ROOT / s) for s in SOURCES} and sha(exe) == record['executable_sha256'], 'Provenance changed during run'
        record['passed'] = True
    finally:
        out_path.write_text(json.dumps(record, indent=1) + '\n')
        summary = {k: record.get(k) for k in ('passed', 'exit_code', 'elapsed_s', 'executable_sha256')}
        if 'report' in record:
            report = record['report']
            summary.update({'checks': report['check_count'], 'failed_checks': report['failed_checks'], 'geometry': report['geometry'],
                            'placed': report['placed'], 'screen': report['screen'], 'rows': report['rows']})
        print(json.dumps(summary, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
