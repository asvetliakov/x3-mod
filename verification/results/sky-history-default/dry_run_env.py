"""Resolved sky-history environment of `tools/manage.py launch --bottle X3 --dry-run`
(never launches): bare, the Run 68 A TAA set with the new defaults, the opt-out, and
TAA without an age program. Prints only the X3M_TAA / X3M_TAA_SKY_HISTORY* variables."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
TAA = ['--ownership', '--object-trace', '--object-lifetime', '--motion-output', '--taa']
AGE = ['--taa-far-stabiliser', '0.985', '--taa-thin-region', '0.97']
CASES = {
    'bare': [],
    'taa_age_default': TAA + AGE,
    'taa_age_optout': TAA + AGE + ['--taa-sky-history', 'loose', '--taa-sky-history-exit-px', '0'],
    'taa_no_age_default': TAA,
}
for name, extra in CASES.items():
    run = subprocess.run([sys.executable, str(ROOT / 'tools/manage.py'), 'launch', '--bottle', 'X3', '--dry-run', *extra],
                         capture_output=True, text=True)
    if run.returncode:
        print(name, 'rc=%d' % run.returncode, run.stderr.strip().splitlines()[-1:])
        continue
    env = json.loads(run.stdout)['env']
    print(name, {k: env.get(k) for k in ('X3M_TAA', 'X3M_TAA_SKY_HISTORY', 'X3M_TAA_SKY_HISTORY_BAND_PX', 'X3M_TAA_SKY_HISTORY_EXIT_PX')})
