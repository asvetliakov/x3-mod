#!/usr/bin/env python3
"""Twin sizes over the local original corpus (verification/probe/engine_light_structure.cpp): the largest twin's
weighted slots and DWORDs, and the twin-minus-base slot delta of the plate twins (the gained sets) against the others.
Needs the local corpus (/tmp/x3-shader-sweep/programs or X3M_SHADER_PROGRAM_DIRECTORY) and a host C++ compiler; prints
one JSON object. Run from the repository root: python3 verification/results/engine-light/plate_slots.py"""
import json
import os
import shutil
import subprocess
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROGRAMS = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
with tempfile.TemporaryDirectory(prefix='x3-plate-slots-') as tmp:
    exe = Path(tmp) / 'structure'
    subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2', str(ROOT / 'verification/probe/engine_light_structure.cpp'),
                    str(ROOT / 'src/renderer/linear_material.cpp'), str(ROOT / 'src/renderer/material_motion.cpp'), '-o', str(exe)],
                   check=True)
    out = json.loads(subprocess.run([str(exe), str(PROGRAMS), tmp], check=True, capture_output=True, text=True).stdout)
    largest = dict(slots=0, words=0)
    deltas = Counter()
    for row in out['rows']:
        for name, entry in row['sets'].items():
            if entry.get('engine') != 1:
                continue
            twin = Path(tmp) / f"{row['name']}-{name}-twin.bin"
            words = twin.stat().st_size // 4
            if entry['twin_slots'] > largest['slots']:
                largest = dict(slots=entry['twin_slots'], words=words, program=row['name'], set=name,
                               base_slots=entry['base_slots'])
            largest['words_max'] = max(largest.get('words_max', 0), words)
            deltas[(name, entry['gain'], entry['twin_slots'] - entry['base_slots'])] += 1
print(json.dumps(dict(twins=out['twins'], largest=largest,
                      deltas=[dict(set=s, plate=g, delta=d, programs=n) for (s, g, d), n in sorted(deltas.items())]), indent=1))
