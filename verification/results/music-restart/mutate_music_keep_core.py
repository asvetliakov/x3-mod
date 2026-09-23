#!/usr/bin/env python3
"""Mutation check of the music-keep host harness (verification/analysis/test_music_keep.py HARNESS).

Compiles the harness against copies of src/proxy/music_keep_core.h with one rule removed each and
prints how many harness checks fail; every mutation must fail at least one. Scratch files live in a
temporary directory; nothing in the tree is modified. Host compiler only, no Wine, no game.
Output beside this script: mutate_music_keep_core_out.txt.
"""
import importlib.util
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MUTATIONS = {
    'no_see_through': ('    if (!overlay_claims(at, actual, length, claims, count)) return false;   // a foreign write over a live claim: fail closed\n', ''),
    'overlay_over_foreign': ('        if (!own) { all_own = false; continue; }   // not our patch: leave the bytes as read (the comparison fails)\n', ''),
    'skip_all_without_gate': ('    if (mode == StopMode::skip_all && !skip_all_available) mode = StopMode::paused;\n', ''),
    'seek_honours_skip_all_hold': ('    if (mine && mine->mode == StopMode::skip_all) mine = nullptr;   // flag 2 alone decides for a skip_all record\n', ''),
    'status_ignores_active_flag': ('    if (active != 0 || (input_flags & run_in_background_bit)) return false;\n', '    if (input_flags & run_in_background_bit) return false;\n'),
    'status_ignores_run_in_background': ('    if (active != 0 || (input_flags & run_in_background_bit)) return false;\n', '    if (active != 0) return false;\n'),
    'status_any_hold_kind': ('    return h && h->mode == StopMode::skip_all;\n', '    return h != nullptr;\n'),
    'acquire_resets_live_site': ('        if (ops.live(s.site)) { *reason = "site_live"; return false; }   // an unrestored claim (failed rollback): refuse, never forget it\n', ''),
    'no_restore_on_failed_fresh_push': ('        if (!s.users && !ops.restore(s.site)) *reason = "rollback_failed";\n', ''),
    'release_restores_early': ('    if (--s.users) return true;\n', '    --s.users;\n'),
}


def main():
    sys.path.insert(0, str(ROOT / 'verification/probe'))
    spec = importlib.util.spec_from_file_location('test_music_keep', ROOT / 'verification/analysis/test_music_keep.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    compiler = shutil.which('clang++') or shutil.which('c++')
    core = (ROOT / 'src/proxy/music_keep_core.h').read_text()
    ok = True
    with tempfile.TemporaryDirectory(prefix='x3-music-mutate-') as temporary:
        d = Path(temporary)
        (d / 'h.cpp').write_text(module.HARNESS)
        for name, (old, new) in MUTATIONS.items():
            if core.count(old) != 1:
                print(f'{name} anchor_missing'); ok = False; continue
            (d / 'music_keep_core.h').write_text(core.replace(old, new))
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-I', str(d), str(d / 'h.cpp'), '-o', str(d / 'h')], capture_output=True, text=True)
            if build.returncode:
                print(f'{name} build_failed'); ok = False; continue
            run = subprocess.run([str(d / 'h')], capture_output=True, text=True, timeout=120)
            fails = [l[5:] for l in run.stdout.splitlines() if l.startswith('FAIL ')]
            print(f'{name} failed_checks={len(fails)} first="{fails[0] if fails else ""}"')
            ok = ok and bool(fails)
    print('result', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
