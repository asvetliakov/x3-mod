#!/usr/bin/env python3
"""Build and run the submit-phase CPU fixture under the bottle's Wine.

The fixture (verification/probe/submit_phase_cpu_fixture.cpp) installs the
production X3M_SUBMIT_PHASES group on a synthetic body holding all twenty-two
proved spans and checks register/flag/XMM/MXCSR/x87/LastError preservation
against the unhooked body on the hit, miss and block-skip paths, the pairing
and counters, the sample conversion, owner admission, rollback, byte-mismatch
and duplicate-claim refusal, late-window refusal, and measures the context
stub's dispatch cost. Never launches the game. Run through the lock:

    X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
        python3 verification/probe/run_submit_phase_cpu.py
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle  # noqa: E402
from build_submit_phase_cpu import build  # noqa: E402
from game_guard import game_running  # noqa: E402

SUMMARY = re.compile(r'^SUBMIT PHASE CPU (.*)$', re.M)


def fields(text):
    return dict(token.split('=', 1) for token in text.split() if '=' in token)


def run(no_build=False):
    report = {} if no_build else build()
    exe = ROOT / 'build/verification/submit-phases/submit_phase_cpu_fixture.exe'
    if not exe.is_file():
        raise SystemExit(f'fixture missing: {exe}')
    assert not game_running(), 'the game is running'
    command = [bottle.WINE] + bottle.wine_args() + ['--workdir', str(exe.parent), str(exe)]
    started = datetime.datetime.now()
    completed = subprocess.run(command, env=dict(os.environ), stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, timeout=1800)
    elapsed = (datetime.datetime.now() - started).total_seconds()
    text = completed.stdout
    (exe.parent / 'stdout.txt').write_text(text)
    (exe.parent / 'stderr.txt').write_text(completed.stderr)
    summary = SUMMARY.search(text)
    counts = fields(summary.group(1)) if summary else {}
    failures = [line for line in text.splitlines() if line.startswith('FAIL ')]
    bench = [line for line in text.splitlines() if line.startswith('SUBMIT PHASE BENCH')]
    record = {
        'result': 'PASS' if completed.returncode == 0 and counts.get('failures') == '0' else 'FAIL',
        'bottle': bottle.describe(),
        'exit': completed.returncode,
        'elapsed_s': round(elapsed, 1),
        'summary': counts,
        'failures': failures[:20],
        'benchmark_lines': bench,
        'handler_sha256': report.get('cpu_audit', {}).get('sha256'),
        'stdout': str((exe.parent / 'stdout.txt').relative_to(ROOT)),
    }
    out = bottle.results_dir(ROOT) / 'submit_phase_cpu.json'
    out.write_text(json.dumps(record, indent=2) + '\n')
    record['record'] = str(out.relative_to(ROOT))
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true', help='run the existing fixture binary')
    args = parser.parse_args()
    record = run(args.no_build)
    print(json.dumps({k: record[k] for k in ('result', 'exit', 'elapsed_s', 'summary', 'failures', 'benchmark_lines', 'record')}, indent=2))
    raise SystemExit(record['result'] != 'PASS')
