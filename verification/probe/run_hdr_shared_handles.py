#!/usr/bin/env python3
"""Fresh-build hidden standalone interop contract probe; no game or install."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
source = root / 'verification/probe/hdr_shared_handles.cpp'
exe = root / 'verification/probe/build/hdr_shared_handles.exe'
results = root / 'verification/results'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
sources = lambda: {str(p.relative_to(root)): sha(p) for p in (source, Path(__file__))}
before = sources()
build = ['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', str(source), '-o', str(exe), '-luser32', '-ldxguid']
subprocess.run(build, check=True, timeout=45)
assert sources() == before
command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine', '--bottle', 'Steam', '--no-update', '--workdir', str(exe.parent), str(exe)]
report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), command=command,
              build_command=build, sources_sha256=before, executable_sha256=sha(exe),
              process_override='d3d9=b', game_launched=False, timeout_seconds=45,
              scope='Hidden standalone API and pixel-aliasing probe. CPU readback is verification only.')
try:
    with (results/'hdr-shared-handles.txt').open('w') as out, (results/'hdr-shared-handles-wine.log').open('w') as err:
        report['exit_code'] = subprocess.run(command, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), stdout=out, stderr=err, timeout=45).returncode
except subprocess.TimeoutExpired:
    report.update(exit_code=None, timed_out=True)
report['sources_and_executable_unchanged'] = sources() == before and sha(exe) == report['executable_sha256']
text = (results/'hdr-shared-handles.txt').read_text()
report['pixel_controls'] = [line for line in text.splitlines() if line.startswith('SHARING_CONTROL ')]
report['completed'] = report['exit_code'] == 0 and report['sources_and_executable_unchanged'] and len(report['pixel_controls']) == 2 and 'PROBE_COMPLETED' in text
report['report_sha256'] = sha(results/'hdr-shared-handles.txt')
(results/'hdr-shared-handles-summary.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
print(text)
raise SystemExit(0 if report['completed'] else 1)
