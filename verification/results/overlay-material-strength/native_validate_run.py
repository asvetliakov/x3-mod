#!/usr/bin/env python3
"""Run native_validate.exe (built from verification/results/hull-widen-native-validator/native_validate.cpp) over the
strength twins of a scratch dir in 15 per-form batches (the Windows command line holds about 32 KB; 1,500 names do
not fit one call), write SCRATCH/native_twins.txt and fold it with native_validate_summary.py. Usage (repo root):
python3 native_validate_run.py SCRATCH [EXE=SCRATCH/native_validate.exe]"""
import subprocess
import sys
from pathlib import Path
S = Path(sys.argv[1])
exe = Path(sys.argv[2]) if len(sys.argv) > 2 else S / 'native_validate.exe'
kinds = ('motion', 'fill', 'gain', 'widen', 'share', 'sgain', 'swiden', 'emotion', 'efill', 'egain', 'ewiden', 'eshare',
         'esgain', 'eswiden', 'smotion')
twins = S / 'twins'
with open(S / 'native_twins.txt', 'w') as out:
    for kind in kinds:
        names = sorted(p.name for p in twins.glob(f'*-{kind}-twin.bin'))
        if not names:
            continue
        run = subprocess.run([str(exe)] + names, cwd=twins, capture_output=True, text=True)
        out.write(run.stdout)
        if run.returncode:
            print(kind, 'rc', run.returncode, run.stderr[:200])
subprocess.run([sys.executable, str(Path(__file__).with_name('native_validate_summary.py')), str(S)], check=True)
