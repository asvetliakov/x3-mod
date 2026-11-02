#!/usr/bin/env python3
"""Mutation check of the FL chase camera fixture: each mutation of src/proxy/chase_camera.cpp is built into the fixture
(a mutated copy beside the original, removed afterwards) and must make it fail. Writes mutation_check.txt beside this
script. Needs MinGW i686 and WSL interop.

usage: python3 -I verification/results/fl-chase-camera/mutation_check.py
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
import test_fl_chase_camera as t  # noqa: E402

SOURCE = ROOT / 'src/proxy/chase_camera.cpp'
MUTANT = ROOT / 'src/proxy/chase_camera_fl_mutant.cpp'
MUTATIONS = {
    'no_cpu_state_boundary': ('    x3m::PreserveCpuState cpu;\n', '\n'),
    'unbound_admitted_without_proof': ('view_object == 0 && chase_transition::active_control_cockpit(cockpit)',
                                       'view_object == 0'),
    'unbound_never_admitted': ('view_object == 0 && chase_transition::active_control_cockpit(cockpit)', 'false'),
    'no_readback_check': ('if (okay && fl_readback_differs()) {', 'if (okay && fl_readback_differs() && false) {'),
    'restore_only_owned_bytes': ('rollback = fl_force_restore() ? "restored" : "failed";',
                                 'rollback = (false && fl_force_restore()) || engine_patch::restore(site) ? "restored" : "failed";'),
}
rows = []
text = SOURCE.read_text()
try:
    for name, (old, new) in MUTATIONS.items():
        assert text.count(old) == 1, name
        MUTANT.write_text(text.replace(old, new))
        sources = tuple(('src/proxy/chase_camera_fl_mutant.cpp', f) if s == 'src/proxy/chase_camera.cpp' else (s, f)
                        for s, f in t.SOURCES)
        saved, t.SOURCES = t.SOURCES, sources
        try:
            with tempfile.TemporaryDirectory(prefix='x3-fl-cc-mut-') as tmp:
                exe, _ = t.build(tmp)
                run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)
        finally:
            t.SOURCES = saved
        fails = [l.split()[1] for l in run.stdout.splitlines() if l.startswith('FAIL ')]
        rows.append(f'{name} exit={run.returncode} failed_checks={len(fails)} first={",".join(fails[:4])}')
finally:
    MUTANT.unlink(missing_ok=True)
out = Path(__file__).resolve().parent / 'mutation_check.txt'
out.write_text('\n'.join(rows) + '\n')
print('\n'.join(rows))
sys.exit(0 if all(' exit=0 ' not in r for r in rows) else 1)
