#!/usr/bin/env python3
"""Build (host only) and run the own sun hook's fixture in the X3 bottle; one compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_own_sun_hook.py
The hook fixture is a plain CPU executable (the probe's own-sun answer and forwarding; the lens bracket and the
lens-flare gain's GPU fixture were deleted on 2026-11-02). --no-build runs the existing binary. Never launches the game.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
import bottle
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/own-sun-hook'
HOOK = BUILD / 'own_sun_hook_fixture.exe'
SOURCES = ('src/proxy/engine_memory.cpp', 'src/proxy/own_sun_hook.cpp', 'src/proxy/own_sun_hook.h', 'src/proxy/own_sun_hook_core.h',
           'src/proxy/own_sun_core.h', 'src/proxy/engine_patch.cpp', 'src/proxy/cpu_state.h',
           'verification/probe/own_sun_hook_fixture.cpp', 'verification/probe/build_own_sun_hook.py',
           'verification/probe/run_own_sun_hook.py')
EXPECTED_HOOK_CHECKS = 40  # a run that skips a section is not a pass (46 until the lens bracket's six checks went, 2026-11-02)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = int(value) if re.fullmatch(r'-?\d+', value) else float(value)
        except ValueError:
            out[key] = value
    return out


def parse_hook(text):
    total = re.search(r'OWN SUN HOOK checks=(\d+) failures=(\d+)', text)
    counters = next((fields(l) for l in text.splitlines() if l.startswith('COUNTERS ')), None)
    return {'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None, 'counters': counters,
            'failure_lines': [l for l in text.splitlines() if l.startswith('FAIL')][:40]}


def accept_hook(record):
    return record['exit_status'] == 0 and record['failures'] == 0 and record['checks'] == EXPECTED_HOOK_CHECKS


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    args = sys.argv[1:]
    build = None
    if '--no-build' not in args:
        import build_own_sun_hook
        build = build_own_sun_hook.build()
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(name), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'module_audit': build['module_audit'] if build else None}
    results = bottle.results_dir(ROOT)
    ok = True
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(HOOK)], capture_output=True, text=True, timeout=600)
    record['hook'] = dict(parse_hook(run.stdout), exit_status=run.returncode, elapsed_s=round(time.time() - started, 1), executable_sha256=sha(HOOK))
    (results / 'own-sun-hook.txt').write_text(run.stdout)
    ok = accept_hook(record['hook']) and ok
    record['passed'] = bool(ok) and record['sources'] == {s: sha(ROOT / s) for s in SOURCES}
    (results / 'own-sun-hook.json').write_text(json.dumps(record, indent=1) + '\n')
    summary = {'passed': record['passed'], 'hook': {k: record['hook'][k] for k in ('checks', 'failures', 'exit_status', 'counters', 'failure_lines')}}
    print(json.dumps(summary, indent=1))
    sys.exit(0 if record['passed'] else 1)


if __name__ == '__main__':
    main()
