#!/usr/bin/env python3
"""Fresh-build original object-lifetime ABI/exception/rollback regression."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

INPUTS = [
    'src/proxy/object_lifetime.cpp', 'src/proxy/object_lifetime.h',
    'verification/probe/object_lifetime.cpp', 'verification/probe/build_object_lifetime.sh',
    'verification/probe/run_object_lifetime.py',
]
WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'


def run(root):
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def sources():
        return {name: digest(root / name) for name in INPUTS}

    results = root / 'verification/results'
    results.mkdir(parents=True, exist_ok=True)
    summary = results / 'object-lifetime-summary.json'
    data = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                game_launched=False, fresh_build=True, passed=False)
    # Invalidate any prior PASS before even reading source inputs. An interrupted
    # or failed invocation must never leave a previous result looking current.
    summary.write_text(json.dumps(data, indent=2) + '\n')
    try:
        data['sources_before'] = sources()
        exe = root / 'verification/probe/build/object_lifetime.exe'
        report = results / 'object-lifetime.txt'
        wine_log = results / 'object-lifetime-wine.log'
        build = subprocess.run(['sh', str(root / 'verification/probe/build_object_lifetime.sh')],
                               capture_output=True, timeout=60)
        (results / 'object-lifetime-build.txt').write_bytes(build.stdout + build.stderr)
        data.update(build_exit=build.returncode, sources_after_build=sources())
        if build.returncode == 0 and data['sources_before'] == data['sources_after_build']:
            data['executable_sha256'] = digest(exe)
            command = [WINE, '--bottle', 'Steam', '--no-update', '--workdir', str(exe.parent), str(exe)]
            data['command'] = command
            with report.open('wb') as out, wine_log.open('wb') as err:
                data['exit_code'] = subprocess.run(command, stdout=out, stderr=err,
                                                  env=dict(os.environ), timeout=90).returncode
            data['executable_sha256_after_run'] = digest(exe)
            data['sources_after_run'] = sources()
            data['report_sha256'] = digest(report)
            lines = report.read_text().splitlines()
            terminal = re.compile(r'RESULT (PASS|FAIL) checks=(\d+) failures=(\d+) backend_calls=(\d+)')
            matches = [terminal.fullmatch(line) for line in lines if line.startswith('RESULT ')]
            match = matches[0] if len(matches) == 1 else None
            last = next((line for line in reversed(lines) if line.strip()), '')
            if match:
                data.update(checks=int(match[2]), failures=int(match[3]), backend_calls=int(match[4]))
            data['passed'] = bool(data['exit_code'] == 0 and match and match[0] == last and
                                  match[1] == 'PASS' and data['failures'] == 0 and data['checks'] > 0 and
                                  data['sources_before'] == data['sources_after_run'] and
                                  data['executable_sha256'] == data['executable_sha256_after_run'])
    except Exception as error:
        data.update(passed=False, error=f'{type(error).__name__}: {error}')
    summary.write_text(json.dumps(data, indent=2) + '\n')
    return data


def main():
    data = run(Path(__file__).resolve().parents[2])
    print(json.dumps({k: v for k, v in data.items() if not k.startswith('sources_')}, indent=2))
    return 0 if data['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
