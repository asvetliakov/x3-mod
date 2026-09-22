#!/usr/bin/env python3
"""Stored-density route evidence: the route bridge, its baseline build and the process-exit fixture.

  build-exit --output DIR                     cross-compile the exit DLL and executable (no Wine)
  run   --output DIR --cases CASES.txt        only under X3M_FIXTURE_BOTTLE=X3 wine_lock.py:
        DIR/build and DIR/baseline-build come from fog_route_bridge_build.py (the latter with
        --baseline from the pinned pre-wiring tree 6f16dbf6, the parent of 39c98242, checked out as
        a scratch git worktree); runs baseline, bridge, exit fixture in turn
  check --output DIR [--summary FILE]         validate the retained logs, write the compact summary
Never launches the game and never rebuilds a DLL of the proxy."""
import argparse, hashlib, json, os, re, subprocess, sys, time
from pathlib import Path
import bottle
import fog_route_bridge_check as bridge
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
         '-static', '-static-libgcc', '-static-libstdc++']
EXIT_REQUIRED = {'exit_%s_%s' % (case, name) for case in ('idle', 'midfill') for name in ('worker_abandoned_returns_in_bound', 'dllmain_detach_precedes_static_destructor')}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def windows(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def build_exit(out):
    out.mkdir(parents=True, exist_ok=True)
    dll, exe = out / 'fog_density_exit.dll', out / 'fog_density_exit_fixture.exe'
    if dll.exists() or exe.exists():
        raise ValueError('refuse overwrite of a frozen exit fixture; choose a new output directory')
    sources = [ROOT / 'verification/probe/fog_density_exit_dll.cpp', ROOT / 'src/fog/fog_density_cache.cpp', ROOT / 'src/fog/fog_density_generator.cpp']
    commands = [['i686-w64-mingw32-g++', *FLAGS, '-shared', *map(str, sources), '-o', str(dll)],
                ['i686-w64-mingw32-g++', *FLAGS, '-municode', str(ROOT / 'verification/probe/fog_density_exit_fixture.cpp'), '-o', str(exe)]]
    for command in commands:
        subprocess.run(command, check=True)
    inputs = [*sources, ROOT / 'verification/probe/fog_density_exit_fixture.cpp', ROOT / 'src/fog/fog_density_cache.h', ROOT / 'src/fog/fog_density_generator.h']
    record = dict(dll_sha256=digest(dll), executable_sha256=digest(exe), commands=commands, inputs={str(p.relative_to(ROOT)): digest(p) for p in inputs})
    (out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def run(out, cases):
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise ValueError('fixture requires X3M_FIXTURE_BOTTLE=X3')
    if (out / 'execution.json').exists():
        raise ValueError('this output already holds a run')
    steps = []
    def step(name, directory, command, timeout):
        built = json.loads((directory / 'build.json').read_text())
        executable = Path(command[0])
        if digest(executable) != built.get('sha256', built.get('executable_sha256')):
            raise ValueError('executable changed since build: ' + name)
        start = time.monotonic()
        try:
            done = subprocess.run([bottle.WINE, *bottle.wine_args(), *command], capture_output=True, timeout=timeout, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
            code, stdout, stderr = done.returncode, done.stdout, done.stderr
        except subprocess.TimeoutExpired as error:  # a hang is a result, not a crash of the runner
            code, stdout, stderr = 'timeout', error.stdout or b'', error.stderr or b''
        (out / (name + '.log')).write_bytes(stdout.replace(b'\r\n', b'\n')); (out / (name + '.stderr.txt')).write_bytes(stderr)
        steps.append(dict(name=name, returncode=code, seconds=time.monotonic() - start, executable_sha256=digest(executable), log_sha256=digest(out / (name + '.log'))))
    step('baseline', out / 'baseline-build', [str(out / 'baseline-build/fog_route_bridge.exe'), windows(cases)], 300)
    step('bridge', out / 'build', [str(out / 'build/fog_route_bridge.exe'), windows(cases)], 900)
    step('exit', out / 'exit-build', [str(out / 'exit-build/fog_density_exit_fixture.exe'), windows(out / 'exit-build/fog_density_exit.dll'), windows(out)], 400)
    record = dict(steps=steps, bottle=bottle.describe(), cases_sha256=digest(cases))
    (out / 'execution.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def check(out, summary):
    execution = json.loads((out / 'execution.json').read_text())
    codes = {s['name']: s['returncode'] for s in execution['steps']}
    if any(codes[name] != 0 for name in ('baseline', 'bridge', 'exit')):
        raise ValueError('a fixture did not exit 0: %r' % codes)
    report = bridge.validate((out / 'bridge.log').read_text(), (out / 'baseline.log').read_text())
    text = (out / 'exit.log').read_text()
    rows = re.findall(r'^CHECK (\S+) (PASS|FAIL)$', text, re.M)
    if not re.search(r'^RESULT fog_density_exit checks=%d PASS$' % len(rows), text, re.M) or {n for n, r in rows if r == 'PASS'} != EXIT_REQUIRED or len(rows) != len(EXIT_REQUIRED):
        raise ValueError('exit fixture incomplete or failed')
    exits = [dict(re.findall(r'(\w+)=(\S+)', line)) for line in re.findall(r'^EXIT (.*)$', text, re.M)]
    controls = [dict(re.findall(r'(\w+)=(\S+)', line)) for line in re.findall(r'^EXIT_CONTROL (.*)$', text, re.M)]
    if len(exits) != 4 or len(controls) != 2:
        raise ValueError('exit fixture cases missing')
    result = dict(schema=1, result='PASS', bridge_checks=report['checks'], density=report['density'], exit_checks=len(rows), exit_cases=exits, exit_controls=controls,
                  limits=report['limits'], steps=execution['steps'], bottle=execution['bottle'])
    summary.parent.mkdir(parents=True, exist_ok=True)
    summary.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('build-exit', 'run', 'check'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cases', type=Path)
    parser.add_argument('--summary', type=Path, default=ROOT / 'verification/results/fog-density-route/summary.json')
    a = parser.parse_args()
    out = a.output.resolve()
    if a.command == 'build-exit':
        print(json.dumps({k: v for k, v in build_exit(out / 'exit-build').items() if k.endswith('sha256')}))
    elif a.command == 'run':
        if a.cases is None: parser.error('run needs --cases')
        record = run(out, a.cases.resolve()); print(json.dumps([(s['name'], s['returncode'], round(s['seconds'], 1)) for s in record['steps']]))
        return 0 if all(s['returncode'] == 0 for s in record['steps']) else 1
    else:
        result = check(out, a.summary); print('PASS bridge_checks=%d exit_checks=%d' % (result['bridge_checks'], result['exit_checks']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
