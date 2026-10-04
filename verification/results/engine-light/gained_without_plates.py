#!/usr/bin/env python3
"""The gained option sets' twins without the nozzle-plate weight (verification/probe/engine_light_structure.cpp over
the local original corpus): per set, the programs whose twin carries no plate and whether their transform applied the
light-map gain at all (gain 0: no gain, so nothing for the plates to suppress), with the family from
eye_normal_registers.py. Run from the repository root: python3 verification/results/engine-light/gained_without_plates.py"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROGRAMS = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
derived = json.loads(subprocess.run([sys.executable, str(ROOT / 'verification/results/engine-light/eye_normal_registers.py')],
                                    capture_output=True, text=True, check=True).stdout)
families = {'ps_' + k: v['family'] for k, v in derived['rows'].items()}
with tempfile.TemporaryDirectory(prefix='x3-gained-') as tmp:
    exe = Path(tmp) / 'structure'
    subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2', str(ROOT / 'verification/probe/engine_light_structure.cpp'),
                    str(ROOT / 'src/renderer/linear_material.cpp'), str(ROOT / 'src/renderer/material_motion.cpp'), '-o', str(exe)],
                   check=True)
    out = json.loads(subprocess.run([str(exe), str(PROGRAMS), tmp], check=True, capture_output=True, text=True).stdout)
result = {}
for set_name in ('gain', 'widen', 'share_gain', 'share_widen'):
    rows = []
    for row in out['rows']:
        e = row['sets'][set_name]
        if e.get('engine') == 1 and row['name'] in families and families[row['name']] != 'asteroid':
            rows.append((row['name'], families[row['name']], e['gain']))
    result[set_name] = dict(twins=len(rows), gained=sum(g for _, _, g in rows),
                            without_gain=sorted({(n, f) for n, f, g in rows if not g}))
print(json.dumps(result, indent=1))
