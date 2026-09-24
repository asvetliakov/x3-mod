#!/usr/bin/env python3
"""--cursor-reassert and the window-thread hooks on the bottle: one Wine run of the CMake-built
fixture, a compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cursor_reassert.py --exe <build>/cursor_reassert_fixture.exe
The fixture (verification/probe/cursor_reassert_fixture.cpp with the production
src/proxy/cursor_reassert.cpp and window_trace.cpp) drives the state machine with
synthetic WM_ACTIVATE messages and gates, runs the real balanced Win32 sequence in the
game's cursor state (asserting one firing, the order and the ShowCursor counts), checks
the WH_CALLWNDPROC/WH_CALLWNDPROCRET/WH_GETMESSAGE observers and their removal. Its probe
window stays hidden. This runner never builds it; it parses the COUNT / CURSOR / CHECK /
RESULT lines and writes <results>/cursor-reassert.json beside the raw
<results>/cursor-reassert.txt. docs/architecture/window-mode-and-cursor-fix.md section 4;
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
DEFAULT_EXE = ROOT / 'build/cursor_reassert_fixture.exe'
SOURCES = ('src/proxy/cursor_reassert_core.h', 'src/proxy/cursor_reassert.h', 'src/proxy/cursor_reassert.cpp', 'src/proxy/window_trace_core.h',
           'src/proxy/window_trace.h', 'src/proxy/window_trace.cpp', 'src/proxy/cpu_state.h',
           'verification/probe/cursor_reassert_fixture.cpp', 'verification/probe/run_cursor_reassert.py')
EXPECTED_CHECKS = 36  # light ring 5, stand-in sequence 4, setup 2, hooks 5 (incl. already_hooked), arming 2, game state 1, present step 4, real sequence 2, refusal 1, real gates 2, mismatch 2, trace ring 4, removal 2


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        out[key] = int(value) if re.fullmatch(r'-?\d+', value) else value
    return out


def parse(text):
    """The fixture's report as one dictionary (also the host test's subject)."""
    report = {'checks': [], 'count': None, 'cursor': None, 'real_gates': None, 'drains': [], 'result': None, 'rows': []}
    for line in text.splitlines():
        if line.startswith('CHECK '):
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif line.startswith('COUNT '):
            report['count'] = fields(line)
        elif line.startswith('CURSOR '):
            report['cursor'] = fields(line)
        elif line.startswith('REAL_GATES '):
            report['real_gates'] = fields(line)
        elif line.startswith('DRAIN '):
            report['drains'].append(fields(line))
        elif line.startswith('LOG cursor_reassert frame='):
            report['rows'].append({k: v for k, v in fields(line).items() if k in ('action', 'reason', 'up', 'down', 'balanced', 'disabled')})
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
    assert report['count'] and report['count']['initial_after_hide'] == -1, report['count']
    fired = [(r['up'], r['down'], r['balanced']) for r in report['rows'] if r.get('action') == 'fired']
    # The synthetic firing, optionally the real-gate firing (balanced), then the -2 mismatch.
    assert fired[0] == (0, -1, 1) and fired[-1] == (-1, -2, 0) and all(f == (0, -1, 1) for f in fired[1:-1]) and len(fired) in (2, 3), fired
    assert report['real_gates'] is not None and report['real_gates']['show_count'] == -1, report['real_gates']


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE, help='The CMake-built cursor_reassert_fixture.exe (default build/cursor_reassert_fixture.exe)')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    exe = args.exe.resolve()
    if not exe.is_file():
        sys.exit(f'fixture not built: {exe} (cmake --build <dir> --target cursor_reassert_fixture)')
    results = bottle.results_dir(ROOT)
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'executable': str(exe), 'executable_sha256': sha(exe), 'expected_checks': EXPECTED_CHECKS}
    out_path = results / 'cursor-reassert.json'
    try:
        command = [bottle.WINE, *bottle.wine_args(), '--workdir', str(exe.parent), str(exe)]
        record['command'] = command
        started = time.time()
        run = subprocess.run(command, capture_output=True, text=True, timeout=300)
        record['elapsed_s'] = round(time.time() - started, 1)
        record['exit_code'] = run.returncode
        text_path = results / 'cursor-reassert.txt'
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
            summary.update({'checks': report['check_count'], 'failed_checks': report['failed_checks'], 'count': report['count'],
                            'cursor': report['cursor'], 'real_gates': report['real_gates'], 'drains': report['drains'], 'rows': report['rows']})
        print(json.dumps(summary, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
