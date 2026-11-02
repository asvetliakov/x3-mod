#!/usr/bin/env python3
"""Mutation check of the FL chase lead's update binding (src/engine/fl/chase_lead_fl.cpp, update=lifetime|pose_seam)
against verification/probe/fl_chase_lead.cpp: copies src/ and the fixture into a scratch tree, applies one defect at a
time, builds with test_fl_chase_lead.py's flags and runs the fixture natively (MinGW i686, WSL interop). Writes
lead_mutation_check.txt beside this script. A surviving mutant is listed with the reason it is equivalent.

usage: python3 -I verification/results/fl-chase-followups/lead_mutation_check.py
"""
import re, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'verification/analysis'))
import test_fl_chase_lead as t

FL = 'src/engine/fl/chase_lead_fl.cpp'
MUT = (
    ('token from the pose even with the transition', '    if (lifetime) {\n        const auto t = chase_transition::current_update(cockpit);',
     '    if (false) {\n        const auto t = chase_transition::current_update(cockpit);'),
    ('camera_context publishes without an open update', '    if (lifetime && (!update.generation || !update.serial)) return;\n', ''),
    ('generation from the pose token', 'return lifetime ? chase_transition::generation(cockpit) : current_update(cockpit).generation;',
     'return current_update(cockpit).generation;'),
    ('fallback never closes at the final FOV', '        if (!lifetime) close_update(regs[4]);', '        (void)&close_update;'),
)
lines = []
for label, old, new in MUT:
    with tempfile.TemporaryDirectory() as d:
        tree = Path(d)
        shutil.copytree(ROOT / 'src', tree / 'src')
        (tree / 'verification/probe').mkdir(parents=True)
        shutil.copy(ROOT / 'verification/probe/fl_chase_lead.cpp', tree / 'verification/probe')
        p = tree / FL
        text = p.read_text()
        assert text.count(old) == 1, label
        p.write_text(text.replace(old, new))
        out = tree / 'o'
        out.mkdir()
        objs = []
        ok = True
        for src, flags in t.SOURCES:
            o = out / (Path(src).stem + '.o')
            r = subprocess.run([t.compiler(), *t.COMMON, *flags, '-c', str(tree / src), '-o', str(o)], capture_output=True, text=True)
            ok &= r.returncode == 0
            objs.append(str(o))
        exe = out / 'x.exe'
        r = subprocess.run([t.compiler(), *t.COMMON, '-static', str(tree / 'verification/probe/fl_chase_lead.cpp'), *objs, '-o', str(exe)], capture_output=True, text=True)
        ok &= r.returncode == 0
        if not ok:
            lines.append(f'BUILD_FAILED | {label}')
            continue
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300).stdout
        m = re.search(r'RESULT (\w+) checks=(\d+) failures=(\d+)', run)
        fails = [l for l in run.splitlines() if l.startswith('FAIL')][:4]
        lines.append(f"{m.group(0) if m else 'crashed'} | {label} | " + '; '.join(f[:70] for f in fails))
EQUIVALENT = {
    'camera_context publishes without an open update':
        'equivalent: without an open update the published pose has serial 0, which every gate reads as no pose',
}
lines = [l + ('  [' + EQUIVALENT[l.split(' | ')[1]] + ']' if ' PASS ' in l and l.split(' | ')[1] in EQUIVALENT else '')
         for l in lines]
(HERE / 'lead_mutation_check.txt').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines))
