#!/usr/bin/env python3
"""Mutation check of the FL chase view restore fixture (verification/probe/fl_chase_transition.cpp): copies src/ and
the fixture into a scratch tree, applies one defect at a time to the FL branch, rebuilds and runs the fixture, and
expects a failing run. Writes mutation_check.txt beside this script. Needs MinGW i686 and WSL interop.

usage: python3 -I verification/results/fl-chase-transition/mutation_check.py
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'verification/analysis'))
import test_fl_chase_transition as t  # noqa: E402

CT = 'src/proxy/chase_transition.cpp'
CORE = 'src/proxy/chase_transition_restore_core.h'
MUTATIONS = (
    ('constructor cockpit from [ESP+4] (AP ABI)', CT,
     '        case 0:\n            chase_lead::native_timing_invalidate(0, thread);\n            cockpit = regs[6];',
     '        case 0:\n            chase_lead::native_timing_invalidate(0, thread);\n            field(esp, 4, cockpit);'),
    ('mode-store frame from EBP (AP ABI)', CT, 'const std::uint32_t requested = regs[6], ebp = regs[4];',
     'const std::uint32_t requested = regs[6], ebp = regs[2];'),
    ('controlled object from [ESP+4] (AP ABI)', CT, 'controlled = regs[6];', 'field(esp, 4, controlled);'),
    ('callback selector 0 (AP value)', CORE, 'constexpr std::uint32_t restore_callback_none = 32;',
     'constexpr std::uint32_t restore_callback_none = 0;'),
    ('caller not normalized to preferred VAs', CT, 'caller = S::preferred(caller, fl_base);', 'caller = caller;'),
    ('read-back check skipped', CT, 'if (engine_patch::read_code(s.spec.address, now, 5) && !std::memcmp(now, s.patched, 5))',
     'if (engine_patch::read_code(s.spec.address, now, 5) || !std::memcmp(now, s.patched, 5))'),
    ('observers kept after a refused restore group', CT,
     '    if (observers_claimed && !restore_okay) restore_group(mandatory, site_count);\n', ''),
    ('windows not checked before the claims', CT, 'if (const char* refusal = S::plan(base, read, &fl_failed_window)) return refusal;',
     'S::plan(base, read, &fl_failed_window);'),
)


def run(tree):
    tmp = tree / 'out'
    tmp.mkdir()
    memory = tmp / 'engine_memory.o'
    subprocess.run([t.compiler(), *t.COMMON, '-mno-sse', '-mno-mmx', '-mfpmath=387', '-c',
                    str(tree / 'src/proxy/engine_memory.cpp'), '-o', str(memory)], check=True)
    exe = tmp / 'fx.exe'
    build = subprocess.run([t.compiler(), *t.COMMON, '-fno-exceptions', '-static',
                            str(tree / 'verification/probe/fl_chase_transition.cpp'), str(memory), '-o', str(exe)],
                           capture_output=True, text=True)
    if build.returncode:
        return 'build_failed', 0
    out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300).stdout
    m = re.search(r'RESULT (\w+) checks=\d+ failures=(\d+)', out)
    return (m.group(1), int(m.group(2))) if m else ('crashed', 0)


lines, killed = [], 0
for label, path, old, new in MUTATIONS:
    with tempfile.TemporaryDirectory(prefix='x3-fl-ct-mut-') as d:
        tree = Path(d)
        shutil.copytree(ROOT / 'src', tree / 'src')
        (tree / 'verification/probe').mkdir(parents=True)
        shutil.copy(ROOT / 'verification/probe/fl_chase_transition.cpp', tree / 'verification/probe')
        text = (tree / path).read_text()
        assert text.count(old) >= 1, label
        (tree / path).write_text(text.replace(old, new))
        result, failures = run(tree)
    dead = result != 'PASS'
    killed += dead
    lines.append(f'{"killed" if dead else "SURVIVED"} {label}: {result} failures={failures}')
    print(lines[-1])
lines.append(f'{killed} / {len(MUTATIONS)} killed')
(HERE / 'mutation_check.txt').write_text('\n'.join(lines) + '\n')
print(lines[-1])
sys.exit(0 if killed == len(MUTATIONS) else 1)
