"""Resolved motion-weight environment of `tools/manage.py launch --bottle X3 --dry-run`
(never launches): the Run 70 A TAA set with the new default, the opt-out, --taa-sentinel 1,
and TAA without an age program. Prints only X3M_TAA, X3M_TAA_SENTINEL and X3M_TAA_MOTION_WEIGHT."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
TAA = ['--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa']
AGE = ['--taa-far-stabiliser', '0.985', '--taa-thin-region', '0.97']
CASES = {
    'taa_age_default': TAA + AGE,
    'taa_age_optout': TAA + AGE + ['--taa-motion-weight', '0'],
    'taa_age_sentinel1': TAA + AGE + ['--taa-sentinel', '1'],
    'taa_no_age_default': TAA,
}
for name, extra in CASES.items():
    run = subprocess.run([sys.executable, str(ROOT / 'tools/manage.py'), 'launch', '--bottle', 'X3', '--dry-run', *extra],
                         capture_output=True, text=True)
    if run.returncode:
        print(name, 'rc=%d' % run.returncode, run.stderr.strip().splitlines()[-1:])
        continue
    env = json.loads(run.stdout)['env']
    print(name, {k: env.get(k) for k in ('X3M_TAA', 'X3M_TAA_SENTINEL', 'X3M_TAA_MOTION_WEIGHT')})
