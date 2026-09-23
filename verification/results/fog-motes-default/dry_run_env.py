"""Resolved dust-mote environment of `tools/manage.py launch --bottle X3 --dry-run` (never launches): the fog set
with the Run 70 B/B2 default (omitted), the opt-out 0, an explicit value, and fog without the stored range.
Prints only X3M_VOLUMETRIC_FOG_RANGE, X3M_FOG_DUST_MOTES and X3M_FOG_MOTES_MAX_PX."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
FOG = ['--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa', '--hdr', '--shadow-replay-depth',
       '--shadow-cascades', '250,1500,7500,37500,150000', '--volumetric-fog', '0.02', '--volumetric-fog-cards', 'replace']
STORED = ['--volumetric-fog-range', 'stored']
CASES = {
    'stored_default': FOG + STORED,
    'stored_optout': FOG + STORED + ['--fog-dust-motes', '0'],
    'stored_2048_4': FOG + STORED + ['--fog-dust-motes', '2048,4'],
    'legacy_range': FOG,
}
for name, extra in CASES.items():
    run = subprocess.run([sys.executable, str(ROOT / 'tools/manage.py'), 'launch', '--bottle', 'X3', '--dry-run', *extra],
                         capture_output=True, text=True)
    if run.returncode:
        print(name, 'rc=%d' % run.returncode, run.stderr.strip().splitlines()[-1:])
        continue
    env = json.loads(run.stdout)['env']
    print(name, {k: env.get(k) for k in ('X3M_VOLUMETRIC_FOG_RANGE', 'X3M_FOG_DUST_MOTES', 'X3M_FOG_MOTES_MAX_PX')})
