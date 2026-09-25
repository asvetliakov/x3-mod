#!/usr/bin/env python3
"""Mutation witness for the docking path of the restore ticket (docs/verification/chase-cpu-boundary.md,
2026-09-25). Copies the restore core and its host fixture into a temporary tree, applies one mutation of
src/proxy/chase_transition_restore_core.h at a time and prints the first fixture line: every mutation must
FAIL. No Wine, no build tree; needs a host c++.

usage: python3 verification/results/chase-view-docking/mutate_restore_host.py
"""
import shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
MUTATIONS = [
    ('restore_dock_reset_prefix,4,0}', 'restore_dock_reset_prefix,4,1}'),        # dock warp 0 -> 1
    ('(o.count==6&&st.dock)', '(o.count==6)'),                                     # option gate removed
    ('if(st.pending){st.clear_pending(cancel_second_destruction);return destroy_cancel_pending;}', ''),  # double arming
    ('    st.take_pending();\n', '    st.take_pending();write(s.ebx+1,restore_rear_mode);\n'),  # second write
    ('0x9be97,0x9bcd0,0};', '0x9be97,0x9bcd0,1};'),                                # dock transfer chain
    ('path.reset,path.reset_count', 'restore_reset_prefix,3'),                     # consume prefix per path
    ('    fill(o);\n', '    const auto early=epoch();fill(o);\n',
     'thread,epoch(),path);', 'thread,early,path);'),                             # epoch read before the walk
    ('st.clear_arm(cancel_destructor);st.reset_attempt();return destroy_refused;',
     'st.clear_arm(cancel_destructor);return destroy_refused;'),                    # option-off sequencing drift
]
failed = 0
with tempfile.TemporaryDirectory(prefix='x3-restore-mutate-') as d:
    tree = Path(d)
    (tree / 'src/proxy').mkdir(parents=True); (tree / 'verification/probe').mkdir(parents=True)
    for name in ('chase_transition_core.h', 'chase_transition_identity_core.h', 'chase_transition.h'):
        shutil.copy(ROOT / 'src/proxy' / name, tree / 'src/proxy' / name)
    shutil.copy(ROOT / 'verification/probe/chase_transition_restore_host.cpp', tree / 'verification/probe/')
    core = (ROOT / 'src/proxy/chase_transition_restore_core.h').read_text()
    for mutation in MUTATIONS:
        mutated = core
        for before, after in zip(mutation[::2], mutation[1::2]):
            assert mutated.count(before) == 1, before
            mutated = mutated.replace(before, after)
        before = mutation[0]
        (tree / 'src/proxy/chase_transition_restore_core.h').write_text(mutated)
        subprocess.run(['c++', '-std=c++17', '-O2', '-w', str(tree / 'verification/probe/chase_transition_restore_host.cpp'),
                        '-o', str(tree / 'fixture')], check=True)
        run = subprocess.run([str(tree / 'fixture')], capture_output=True, text=True)
        line = (run.stderr or run.stdout).strip().splitlines()[0]
        failed += line.startswith('FAIL')
        print(f'{before[:44]!r:48} -> {line}')
print(f'mutations={len(MUTATIONS)} caught={failed}')
sys.exit(0 if failed == len(MUTATIONS) else 1)
