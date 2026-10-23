#!/usr/bin/env python3
"""Mutation check for the re-bind's identity and deferred-sector rules (src/proxy/seta_restore_core.h): compiles
verification/probe/seta_restore_host.cpp against six single-line mutations of the core header in a temporary tree and
prints the first failing check of each. Every mutation must fail the harness; ends with RESULT PASS if each one does.

    python3 -I verification/results/seta-script-toggle/rebind_identity_mutations.py
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CXX = shutil.which('c++') or shutil.which('clang++')
MUTATIONS = [
    ('identity check removed', 'if (!script_id || out.new_script_id != script_id) return ShipCheck::identity;', ''),
    ('no_sector cancels again', 'if (out.ship_check == ShipCheck::no_sector) {', 'if (false) {'),
    ('cancel row ship= the new pointer', "out.new_ship = player; // ship= stays the ticket's own (base)",
     'out.new_ship = player; out.ship = player;'),
    ('new flags not read', 'out.new_flags_readable = p.flags(player, out.new_flags);', 'out.new_flags_readable = false;'),
    ('arm without a script id', 'if (!script_read) return refuse(r, refuse_unreadable);', ''),
    ('pending row every entry', 'pending = true;\n', ''),
]
source = (ROOT / 'src/proxy/seta_restore_core.h').read_text()
failed = 0
with tempfile.TemporaryDirectory(prefix='x3-seta-mut-') as d:
    tree = Path(d)
    (tree / 'src/proxy').mkdir(parents=True)
    (tree / 'verification/probe').mkdir(parents=True)
    shutil.copy(ROOT / 'verification/probe/seta_restore_host.cpp', tree / 'verification/probe/')
    for name, old, new in MUTATIONS:
        if source.count(old) != 1:
            sys.exit('mutation anchor not unique: %s' % name)
        (tree / 'src/proxy/seta_restore_core.h').write_text(source.replace(old, new))
        exe = tree / 'h'
        subprocess.run([CXX, '-std=c++17', '-O2', '-w', str(tree / 'verification/probe/seta_restore_host.cpp'), '-o',
                        str(exe)], check=True)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        first = (run.stderr or run.stdout).splitlines()[0]
        caught = run.returncode != 0
        failed += not caught
        print('   [%s] %s: %s' % ('ok' if caught else 'FAIL', name, first))
print('RESULT %s (%d of %d mutations caught)' % ('PASS' if not failed else 'FAIL', len(MUTATIONS) - failed,
                                                 len(MUTATIONS)))
sys.exit(1 if failed else 0)
