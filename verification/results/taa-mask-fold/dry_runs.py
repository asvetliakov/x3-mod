#!/usr/bin/env python3
"""The mask fold's launcher dry runs (docs/verification/temporal-resolve.md, "Mask fold"): the --taa default, the thin-region
source both / screen, the retired --taa-sentinel-stabiliser and --vanilla. Never launches (manage.py launch --dry-run).
Prints one line per run: the exit code, the four variables of interest (absent = -) or the parser error's first line."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = ['--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa', '--hdr', '--sun-shadow-lane']
RUNS = {'taa_default': BASE, 'source_both': BASE + ['--taa-thin-region-source', 'both'],
        'source_screen': BASE + ['--taa-thin-region-source', 'screen'], 'sentinel_0.7': BASE + ['--taa-sentinel-stabiliser', '0.7'],
        'vanilla': ['--vanilla']}
KEYS = ('X3M_TAA_THIN_REGION_SOURCE', 'X3M_TAA_THIN_REGION_SOURCE_DEFAULT', 'X3M_TAA_SENTINEL_STABILISER', 'X3M_TAA_THIN_VOTE')
for name, args in RUNS.items():
    run = subprocess.run([sys.executable, str(ROOT / 'tools/manage.py'), 'launch', '--dry-run', *args], capture_output=True, text=True, cwd=ROOT)
    if run.returncode == 0:
        env = json.loads(run.stdout)['env']
        print(name, 'exit=0', ' '.join(f'{k}={env.get(k, "-")}' for k in KEYS))
    else:
        message = [line for line in run.stderr.splitlines() if 'error:' in line]
        print(name, f'exit={run.returncode}', message[0][message[0].index('error:'):] if message else run.stderr.strip()[-200:])
