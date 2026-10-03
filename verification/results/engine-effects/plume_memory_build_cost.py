#!/usr/bin/env python3
"""Plume review fix P1 (2026-10-03): the build's cost with the per-nozzle memory (engine_plumes_core.h Transients: every
drawn nozzle takes a slot and advances its own flow phase) against the plain build and the build with the plume floor,
on the host (clang -O2), at 30 / 100 / 1,024 records (every record its own identity; past 512 drawn nozzles the window
overflows). Compiles test_engine_plumes.py's own harness (its checks run too) and keeps its BUILD lines: median
microseconds per build of 31 x 20 builds. Host-only, no Wine. Output: plume_memory_build_cost_out.txt beside this script.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
sys.path.insert(0, str(ROOT / 'verification/analysis'))
sys.path.insert(0, str(ROOT))
from verification.analysis import test_engine_plumes as t  # noqa: E402

compiler = shutil.which('clang++') or shutil.which('c++')
with tempfile.TemporaryDirectory(prefix='x3-plume-memory-') as directory:
    source, executable = Path(directory) / 'harness.cpp', Path(directory) / 'harness'
    source.write_text(t.HARNESS.replace('#include <vector>', '#include <vector>\n#include <algorithm>'))
    subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(source), '-o',
                    str(executable)], check=True)
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=True, timeout=300)
lines = [l for l in run.stdout.splitlines() if l.startswith(('BUILD', 'FLOW_KEYED_HOST', 'ATTACK_GAP', 'engine_plumes_core'))]
text = '\n'.join(lines) + '\n'
(Path(__file__).with_name('plume_memory_build_cost_out.txt')).write_text(text)
print(text, end='')
