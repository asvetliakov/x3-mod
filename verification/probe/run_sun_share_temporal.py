#!/usr/bin/env python3
"""Run only the sun-lane temporal mode of an already-built fixture.

Owner command: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py
python3 verification/probe/run_sun_share_temporal.py. Never rebuilds a DLL.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import bottle


def main():
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise SystemExit('Set X3M_FIXTURE_BOTTLE=X3 explicitly.')
    root = Path(__file__).resolve().parents[2]
    exe = root/'verification/probe/build/temporal_pass_fixture.exe'
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    results = bottle.results_dir(root)
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(exe.parent), str(exe),
               r'C:\X3\d3dx9_37.dll', *('Z:'+str(root/p) for p in ('src/temporal/depth_decode.hlsl', 'src/temporal/resolve.hlsl', 'src/temporal/taa_sharpen_ps.hlsl')), 'sun-lane-only']
    report = dict(passed=False, bottle=bottle.describe(), executable_sha256=digest, command=command, game_launched=False)
    start = time.monotonic()
    try:
        with (results/'sun-share-temporal.txt').open('w') as output, (results/'sun-share-temporal-wine.log').open('w') as errors:
            result = subprocess.run(command, stdout=output, stderr=errors, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=120)
        report['exit_code'] = result.returncode
        report['elapsed_seconds'] = time.monotonic()-start
        text = (results/'sun-share-temporal.txt').read_text()
        match = re.search(r'SUN_LANE_PASS histories=(\d+) copy_failures=(\d+) sizes=(\d+) resets=(\d+)', text)
        report['counts'] = list(map(int, match.groups())) if match else None
        report['state_restorations'] = text.count('CHECK sun copy restores hostile state PASS')
        report['executable_unchanged'] = hashlib.sha256(exe.read_bytes()).hexdigest() == digest
        report['passed'] = result.returncode == 0 and report['counts'] == [16, 8, 8, 2]  # two enhanced formats (G32R32F, A32B32G32R32F) x two widths x two frames x two generations and 'FAIL' not in text and report['executable_unchanged']
        if not report['passed']:
            raise RuntimeError('sun-share temporal fixture failed; see scoped log')
    finally:
        (results/'sun-share-temporal-summary.json').write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
