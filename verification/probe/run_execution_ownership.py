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
EXPECTED_NATIVE = {
    'd3d9.dll': '58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf',
    'wined3d.dll': 'f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863',
}
EXPECTED_CASES = ['CASE basic pure=0 PASS', 'CASE basic pure=1 PASS'] + [
    f'CASE loss method={kind} hr={hr} PASS' for kind in range(5)
    for hr in ('80004005', '88760868', '88760869')]

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
    return ({str(p.relative_to(ROOT)): sha(p) for p in paths}
            | {'native/' + name: sha(NATIVE / name) for name in EXPECTED_NATIVE})

def main():
    RESULT.parent.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'game_launched': False, 'scope': 'original native wrapper fixture; test-only per-instance HRESULT injections'}
    RESULT.write_text(json.dumps(report, indent=2) + '\n')
    try:
        require_no_game()
        before = hashes(); report['sources'] = before
        if any(before['native/' + n] != h for n, h in EXPECTED_NATIVE.items()):
            raise RuntimeError('native files differ from reviewed Preview runtime')
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
        lines = LOG.read_text().splitlines(); terminal = [l for l in lines if l.startswith('RESULT')]
        if len(terminal) != 1 or not lines or lines[-1] != terminal[0]: raise RuntimeError('invalid terminal result')
        match = re.fullmatch(r'RESULT PASS checks=(\d+) failures=0', terminal[0])
        if result.returncode or not match or int(match[1]) != 132: raise RuntimeError('native fixture failed')
        cases = [l for l in lines if l.startswith('CASE ')]
        if cases != EXPECTED_CASES: raise RuntimeError('unexpected completed case inventory')
        if hashes() != before or sha(exe) != report['executable_sha256']: raise RuntimeError('source/artifact changed')
        report.update(passed=True, checks=int(match[1]), completed_cases=len(cases),
                      report_sha256=sha(LOG), wine_log_sha256=sha(ERR))
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        RESULT.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'sources'}, indent=2))

if __name__ == '__main__': main()
