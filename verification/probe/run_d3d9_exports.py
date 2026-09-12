#!/usr/bin/env python3
"""Export-table and log-directory runner (W1 and W3 of the native-Windows audit).

Builds the proxy (cmake, incremental) and verification/probe/d3d9_exports_fixture.cpp,
then runs the fixture twice under CrossOver Preview with the proxy next to it
(`--dll d3d9=n,b`): once in a writable directory, where the fixture must
resolve all seventeen system d3d9 export names on the proxy, call the
forwarded and the fallback entry points with the documented results and the
proxy's session log (first line `capture_dir=... source=game`) must carry one
`d3d9_export` line per first-called forwarder; once in a directory made
read-only, where the proxy must fall back to
%LOCALAPPDATA%\\x3-modern-renderer\\captures (`source=localappdata`) and leave
nothing in the read-only directory. Host-side: verification/analysis/
test_d3d9_exports.py parses the export directory from the file.
Run through verification/probe/wine_lock.py; the game must be down.
"""
import datetime
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle  # noqa: E402  CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
from game_guard import game_running  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import pe_exports  # noqa: E402

BUILD = ROOT / 'verification/probe/build'
RESULTS = bottle.results_dir(ROOT)
EXE = BUILD / 'd3d9_exports_fixture.exe'
DLL = ROOT / 'build/d3d9.dll'
WINE = Path(bottle.WINE)
SOURCES = ['src/proxy/loader.cpp', 'src/proxy/d3d9.def', 'src/proxy/capture.cpp', 'verification/probe/d3d9_exports_fixture.cpp',
           'verification/probe/build_d3d9_exports.sh', 'verification/probe/run_d3d9_exports.py', 'tools/analysis/pe_exports.py']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    return dict(re.findall(r'(\w+)=(\S+)', line))


def windows_to_bottle(path):
    """A Windows path of the bottle to its host path: C: is drive_c, any other letter follows the bottle's
    dosdevices/<letter>: link (the worktree is reached through Y: or Z:)."""
    match = re.match(r'^([A-Za-z]):[\\/](.*)$', path)
    assert match, path
    letter, rest = match.group(1).lower(), Path(match.group(2).replace('\\', '/'))
    if letter == 'c':
        return bottle.bottle_dir() / 'drive_c' / rest
    link = bottle.bottle_dir() / 'dosdevices' / f'{letter}:'
    assert link.exists(), (path, link)
    return link.resolve() / rest


def run_case(name, readonly, report):
    directory = BUILD / f'd3d9-exports-{name}-{datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")}'
    directory.mkdir(parents=True)
    shutil.copy(EXE, directory)
    shutil.copy(DLL, directory / 'd3d9.dll')
    started = time.time()
    if readonly:
        directory.chmod(stat.S_IRUSR | stat.S_IXUSR | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
    command = [str(WINE), *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(directory), str(directory / EXE.name)] + (['readonly'] if readonly else [])
    env = dict(os.environ, WINEDLLOVERRIDES='d3d9=n,b', X3M_TELEMETRY='0')
    try:
        completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=180)
    finally:
        if readonly:
            directory.chmod(stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
    text = completed.stdout
    report.append(f'==== {name} exit={completed.returncode}\n{text}')
    (RESULTS / f'd3d9-exports-{name}-wine.log').write_text(completed.stderr)
    lines = text.splitlines()
    assert completed.returncode == 0 and lines and lines[-1].startswith('RESULT PASS '), f'{name}: fixture failed: {text[-800:]}'
    terminal = fields(lines[-1])
    exports = {fields(l)['name']: fields(l)['address'] for l in lines if l.startswith('EXPORT ')}
    assert sorted(exports) == sorted(pe_exports.SYSTEM_D3D9_EXPORTS) and all(a not in ('0', '00000000', '(nil)') for a in exports.values()), (name, exports)
    calls = {fields(l)['name']: fields(l) for l in lines if l.startswith('CALL ')}
    env_lines = {fields(l).get('LOCALAPPDATA') or fields(l).get('USERPROFILE'): l for l in lines if l.startswith('ENV ')}
    localappdata = next((fields(l)['LOCALAPPDATA'] for l in lines if l.startswith('ENV LOCALAPPDATA=')), '-')
    userprofile = next((fields(l)['USERPROFILE'] for l in lines if l.startswith('ENV USERPROFILE=')), '-')
    # The proxy's session log: next to the DLL, or the LOCALAPPDATA fallback.
    game_logs = sorted((directory / 'x3-modern-captures').glob('session-*.log')) if (directory / 'x3-modern-captures').is_dir() else []
    if readonly:
        assert not (directory / 'x3-modern-captures').exists(), f'{name}: the read-only directory received a capture directory'
        base = windows_to_bottle(localappdata) if localappdata != '-' else windows_to_bottle(userprofile) / 'AppData/Local'
        fallback_dir = base / 'x3-modern-renderer' / 'captures'
        candidates = [p for p in sorted(fallback_dir.glob('session-*.log')) if p.stat().st_mtime >= started - 2]
        assert len(candidates) == 1, f'{name}: expected one new session log under {fallback_dir}, found {[p.name for p in candidates]}'
        trace_path = candidates[0]
        expected_source = 'localappdata'
    else:
        assert len(game_logs) == 1, f'{name}: expected one session log next to the DLL, found {len(game_logs)}'
        trace_path = game_logs[0]
        expected_source = 'game'
    trace = trace_path.read_text(errors='replace')
    first = trace.splitlines()[0]
    assert first.startswith('capture_dir=') and fields(first)['source'] == expected_source, (name, first)
    capture_dir = first[len('capture_dir='):].rsplit(' source=', 1)[0]
    assert windows_to_bottle(capture_dir).resolve() == trace_path.parent.resolve(), (name, capture_dir, trace_path)
    export_lines = {fields(l)['name']: fields(l) for l in trace.splitlines() if l.startswith('d3d9_export ')}
    # The shim has no backend export under Wine: the naked forwarder's fallback answered, logged once.
    assert export_lines['Direct3D9EnableMaximizedWindowedModeShim']['forwarded'] == '0', (name, export_lines)
    assert sum(l.startswith('d3d9_export name=Direct3D9EnableMaximizedWindowedModeShim') for l in trace.splitlines()) == 1, name
    on12 = calls['Direct3DCreate9On12']
    on12_object = on12['result'] not in ('00000000', '0', '(nil)')
    assert export_lines['Direct3DCreate9On12'] == {'name': 'Direct3DCreate9On12', 'forwarded': str(int(on12_object)), 'unproxied': str(int(on12_object))}, (name, export_lines)
    on12ex = calls['Direct3DCreate9On12Ex']
    on12ex_object = on12ex['out'] not in ('00000000', '0', '(nil)')
    assert export_lines['Direct3DCreate9On12Ex']['unproxied'] == str(int(on12ex_object)), (name, export_lines)
    if not on12ex_object:
        assert export_lines['Direct3DCreate9On12Ex']['forwarded'] == '0' and on12ex['result'].lower() == '8876086a', (name, on12ex)  # D3DERR_NOTAVAILABLE
    assert 'Direct3DCreate9Ex' not in export_lines and 'DebugSetLevel' not in export_lines, (name, export_lines)  # not called
    assert calls['Direct3D9EnableMaximizedWindowedModeShim']['popped'] == '4' and calls['Direct3D9EnableMaximizedWindowedModeShim']['result'] == '00000000', (name, calls)
    result = {'exit': completed.returncode, 'checks': int(terminal['checks']), 'failures': int(terminal['failures']), 'readonly': readonly,
              'directory': str(directory.relative_to(ROOT)), 'trace': str(trace_path), 'trace_sha256': sha(trace_path), 'capture_dir': capture_dir,
              'source': expected_source, 'localappdata': localappdata, 'userprofile': userprofile, 'exports_resolved': len(exports),
              'calls': calls, 'export_lines': export_lines, 'on12_forwarded': on12_object, 'on12ex_forwarded': on12ex_object,
              'dll_sha256': sha(directory / 'd3d9.dll'), 'exe_sha256': sha(directory / EXE.name), 'env_lines': sorted(env_lines.values())}
    if readonly:
        # The fallback log belongs to the bottle's user profile; keep a copy with the results and remove the original.
        shutil.copy(trace_path, RESULTS / f'd3d9-exports-{name}-capture.log')
        trace_path.unlink()
    else:
        shutil.copy(trace_path, RESULTS / f'd3d9-exports-{name}-capture.log')
    print(f'{name}: exit={completed.returncode} checks={terminal["checks"]} source={expected_source} on12_forwarded={on12_object}', flush=True)
    return result


def main():
    RESULTS.mkdir(exist_ok=True)
    summary_path = RESULTS / 'd3d9-exports-summary.json'
    report_path = RESULTS / 'd3d9-exports.txt'
    result = {'passed': False, 'status': 'RUNNING', 'game_launched': False, 'bottle': bottle.describe(),
              'scope': 'The proxy DLL export table (17 system d3d9 names) resolved and called from a console fixture under CrossOver Preview: forwarded entry points, the naked forwarders\' ret-N fallbacks where the backend lacks the export, and the session-log directory fallback to %LOCALAPPDATA% from a read-only game directory. Not a native-Windows run.',
              'cases': {}}
    save = lambda: summary_path.write_text(json.dumps(result, indent=2) + '\n')
    save()
    report = []
    try:
        assert game_running() == [], 'X3AP running or process inventory failed; postpone'
        assert WINE.is_file(), 'CrossOver Preview Wine missing'
        result['sources_before_build'] = {s: sha(ROOT / s) for s in SOURCES}
        with (RESULTS / 'd3d9-exports-build.log').open('w') as out:
            subprocess.run(['cmake', '--build', 'build', '-j4'], cwd=ROOT, check=True, stdout=out, stderr=subprocess.STDOUT)
            subprocess.run(['sh', 'verification/probe/build_d3d9_exports.sh'], cwd=ROOT, check=True, stdout=out, stderr=subprocess.STDOUT)
        result['binaries'] = {str(p.relative_to(ROOT)): sha(p) for p in (EXE, DLL)}
        table = pe_exports.parse(DLL)
        assert table['names'] == sorted(pe_exports.SYSTEM_D3D9_EXPORTS), table['names']
        result['export_directory'] = table
        result['cases']['writable'] = run_case('writable', False, report)
        save()
        result['cases']['readonly'] = run_case('readonly', True, report)
        assert {s: sha(ROOT / s) for s in SOURCES} == result['sources_before_build'], 'Sources changed during the run'
        result['limits'] = ['Wine backend: three of the four naked forwarders reach Wine spec stubs and are resolved but not called; Direct3D9EnableMaximizedWindowedModeShim exercises the fallback path.',
                            'The read-only case relies on the bottle refusing CreateDirectoryW/_wfopen in a 0555 directory, which stands in for a Windows ACL denial.',
                            'Windows is cross-compiled, not verified.']
        result['passed'] = True; result['status'] = 'PASS'
    except BaseException as error:
        result['status'] = 'FAIL'; result['error'] = repr(error)
        raise
    finally:
        report_path.write_text(''.join(report))
        result['report_sha256'] = sha(report_path) if report_path.exists() else None
        save()
        print(json.dumps({k: v for k, v in result.items() if k in ('status', 'passed', 'error')}))


if __name__ == '__main__':
    main()
