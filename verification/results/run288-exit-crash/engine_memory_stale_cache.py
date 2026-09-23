"""Run the engine-memory host driver against the pre-fix reader (5a9f45f8, baseline
scenarios only) and the working-tree reader; prints each run's FAIL/HOTPATH/result rows.

    PYTHONPATH=verification/probe python3 verification/results/run288-exit-crash/engine_memory_stale_cache.py
"""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from verification.analysis.test_engine_memory_shutdown import build_and_run  # noqa: E402

case = unittest.TestCase()
old = subprocess.run(['git', '-C', str(ROOT), 'show', '5a9f45f8:src/proxy/engine_memory.cpp'],
                     capture_output=True, text=True, check=True).stdout
for name, text, flags in (('pre_fix_5a9f45f8', old, ('-DENGINE_MEMORY_HOST_BASELINE',)),
                          ('working_tree', (ROOT / 'src/proxy/engine_memory.cpp').read_text(), ())):
    run = build_and_run(case, text, flags)
    print(f'{name} exit={run.returncode}')
    for line in run.stdout.splitlines():
        print(f'  {line}')
