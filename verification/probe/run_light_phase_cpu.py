#!/usr/bin/env python3
"""Run synthetic R7 CPU fixture under the owner Wine queue; never launches game."""
import argparse
import datetime
import json
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle  # noqa: E402
from build_light_phase_cpu import build  # noqa: E402
from game_guard import game_running  # noqa: E402

SUMMARY = re.compile(r'^LIGHT PHASE CPU (.*)$', re.M)


def fields(text):
    return dict(token.split('=', 1) for token in text.split() if '=' in token)


def source_inputs():
    """Only this fixture's translation units, local include closure and build driver."""
    paths=set()
    pending=[ROOT/name for name in ('src/proxy/light_phases.cpp','src/proxy/lean_stub.cpp',
             'src/proxy/engine_patch.cpp','verification/probe/light_phase_cpu_fixture.cpp')]
    while pending:
        path=pending.pop().resolve()
        if path in paths:
            continue
        paths.add(path)
        for name in re.findall(r'^#include "([^"]+)"',path.read_text(),re.M):
            include=(path.parent/name).resolve()
            if include.is_file():
                pending.append(include)
    paths.update(ROOT/name for name in ('verification/probe/build_light_phase_cpu.py',
                  'verification/probe/run_light_phase_cpu.py','verification/probe/run_chase_aim_trace.py'))
    return sorted(paths)


def source_digest(paths):
    digest=hashlib.sha256()
    for path in paths:
        digest.update(str(path.relative_to(ROOT)).encode()+b'\0'+path.read_bytes()+b'\0')
    return digest.hexdigest()


def file_digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def run(no_build=False):
    paths=source_inputs()
    source_before=source_digest(paths)
    report = {} if no_build else build()
    exe = ROOT / 'build/verification/light-phases/light_phase_cpu_fixture.exe'
    if not exe.is_file():
        raise SystemExit(f'fixture missing: {exe}')
    if game_running():
        raise SystemExit('the game is running')
    handler=exe.parent/'production_handler.o'
    exe_before=file_digest(exe)
    handler_before=file_digest(handler)
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
    bench = [line for line in text.splitlines() if line.startswith('LIGHT PHASE BENCH')]
    source_after=source_digest(paths)
    exe_after=file_digest(exe)
    handler_after=file_digest(handler)
    stable=source_before==source_after and exe_before==exe_after and handler_before==handler_after
    provenance={'build_mode':'retained' if no_build else 'fresh', 'source_inputs':len(paths),
                'source_sha256_before':source_before,'source_sha256_after':source_after,
                'exe_sha256_before':exe_before,'exe_sha256_after':exe_after,
                'handler_sha256_before':handler_before,'handler_sha256_after':handler_after,
                'stable':stable}
    valid_summary=len(SUMMARY.findall(text))==1 and counts.get('sites')=='2' and counts.get('checks','').isdigit() and int(counts['checks'])>0
    record = {
        'result': 'PASS' if completed.returncode == 0 and valid_summary and counts.get('failures') == '0' and not failures and stable else 'FAIL',
        'bottle': bottle.describe(),
        'exit': completed.returncode,
        'elapsed_s': round(elapsed, 1),
        'summary': counts,
        'failures': failures[:20],
        'benchmark_lines': bench,
        'handler_sha256': handler_before,
        'provenance': provenance,
        'cpu_audit': report.get('cpu_audit'),
        'stdout': str((exe.parent / 'stdout.txt').relative_to(ROOT)),
    }
    out = bottle.results_dir(ROOT) / 'light_phase_cpu.json'
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
