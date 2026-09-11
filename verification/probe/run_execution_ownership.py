#!/usr/bin/env python3
"""Fresh native wrapper fixture in Preview; no game/install and no persistent override."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RESULT = ROOT / 'verification/results/execution-ownership-summary.json'
LOG = ROOT / 'verification/results/execution-ownership.txt'
ERR = ROOT / 'verification/results/execution-ownership-wine.log'
WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
NATIVE = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/syswow64'
NATIVE_FILES = ('d3d9.dll', 'wined3d.dll')

EXPECTED_CASES = ['CASE cpu-state disabled=1 PASS', 'CASE basic pure=0 PASS', 'CASE basic pure=1 PASS'] + [
    f'CASE loss method={kind} hr={hr} PASS' for kind in range(5)
    for hr in ('80004005', '88760868', '88760869')] + [f'CASE thread operation={i} PASS' for i in range(3)] + ['CASE cpu-state disabled=0 PASS', 'CASE dispatch-timing PASS']

def require_no_game():
    active = subprocess.run(['pgrep', '-ifl', 'X3AP.exe'], capture_output=True, text=True, timeout=10)
    if active.returncode == 0: raise RuntimeError('X3 game active; synthetic GPU run refused')
    if active.returncode != 1: raise RuntimeError('cannot establish game absence')

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def hashes():
    paths = [ROOT / 'verification/probe/execution_ownership_fixture.cpp',
             ROOT / 'verification/probe/build_execution_ownership.sh', Path(__file__).resolve(),
             ROOT / 'tools/ownership/generate_d3d9_forwarders.py']
    paths += sorted((ROOT / 'src/ownership').glob('*.cpp')) + sorted((ROOT / 'src/ownership').glob('*.h'))
    return {str(p.relative_to(ROOT)): sha(p) for p in paths}

def native_provenance():
    # No expected DLL version/digest. Only record actual files for test stability.
    recorded = {}
    for name in NATIVE_FILES:
        try: recorded[name] = {'sha256': sha(NATIVE / name)}
        except OSError as error: recorded[name] = {'sha256': None, 'unavailable': str(error)}
    return recorded

def main():
    RESULT.parent.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'game_launched': False, 'scope': 'original native wrapper fixture; test-only per-instance HRESULT injections'}
    RESULT.write_text(json.dumps(report, indent=2) + '\n')
    try:
        require_no_game()
        before = hashes(); report['sources'] = before
        report['native_files_before'] = native_provenance()
        report['compiler'] = subprocess.run(['i686-w64-mingw32-g++', '--version'], capture_output=True,
                                            check=True, text=True, timeout=15).stdout
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_execution_ownership.sh')],
                       check=True, capture_output=True, text=True, timeout=60)
        if before != hashes(): raise RuntimeError('sources changed during build')
        exe = ROOT / 'verification/probe/build/execution_ownership.exe'
        report['executable_sha256'] = sha(exe)
        command = [WINE, '--bottle', 'Steam', '--no-update', '--workdir', str(exe.parent), str(exe)]
        report.update(command=command, process_local_override='d3d9=b', timeout_seconds=90)
        env = dict(os.environ, WINEDLLOVERRIDES='d3d9=b')
        require_no_game()
        with LOG.open('w') as output, ERR.open('w') as errors:
            result = subprocess.run(command, env=env, stdout=output, stderr=errors, timeout=90)
        report['exit_code'] = result.returncode
        report['native_files_after'] = native_provenance()
        if report['native_files_before'] != report['native_files_after']:
            raise RuntimeError('runtime files changed while producing verification evidence')
        lines = LOG.read_text().splitlines(); terminal = [l for l in lines if l.startswith('RESULT')]
        if len(terminal) != 1 or not lines or lines[-1] != terminal[0]: raise RuntimeError('invalid terminal result')
        match = re.fullmatch(r'RESULT PASS checks=(\d+) failures=0', terminal[0])
        if result.returncode or not match or int(match[1]) != 184: raise RuntimeError('native fixture failed')
        cases = [l for l in lines if l.startswith('CASE ')]
        if cases != EXPECTED_CASES: raise RuntimeError('unexpected completed case inventory')
        if hashes() != before or sha(exe) != report['executable_sha256']: raise RuntimeError('source/artifact changed')
        report.update(passed=True, checks=int(match[1]), completed_cases=len(cases),
                      report_sha256=sha(LOG), wine_log_sha256=sha(ERR))
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        if 'native_files_before' in report and 'native_files_after' not in report:
            report['native_files_after'] = native_provenance()
        RESULT.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'sources'}, indent=2))

if __name__ == '__main__': main()
