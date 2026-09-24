"""The four dry runs of the --original-fill launcher default (2026-09-25): prints X3M_ORIGINAL_FILL and its marker per case.
Run from the repository root: python3 verification/results/original-fill-default/dry_runs.py (dry runs only, never launches)."""
import json
import subprocess
import sys

CASES = {'modded --hdr': ['--motion-output', '--hdr'],
         'modded --hdr --original-fill 0': ['--motion-output', '--hdr', '--original-fill', '0'],
         'modded --hdr --linear-materials': ['--motion-output', '--hdr', '--hdr-tonemap', '--linear-materials'],
         'vanilla': ['--vanilla']}
for name, args in CASES.items():
    run = subprocess.run([sys.executable, 'tools/manage.py', 'launch', '--dry-run', *args], capture_output=True, text=True)
    if run.returncode:
        print(f'{name}: exit={run.returncode} {run.stderr.strip()[-200:]}')
        continue
    env = json.loads(run.stdout)['env']
    print(f"{name}: exit=0 X3M_ORIGINAL_FILL={env.get('X3M_ORIGINAL_FILL')} X3M_ORIGINAL_FILL_DEFAULT={env.get('X3M_ORIGINAL_FILL_DEFAULT')} "
          f"X3M_MATERIAL_FILL={env.get('X3M_MATERIAL_FILL')} X3M_HDR={env.get('X3M_HDR')}")
