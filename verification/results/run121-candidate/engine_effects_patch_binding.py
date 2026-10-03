"""Run121 candidate: does the engine-effects-patch fixture record bind to a tree?

Usage (from the tree root): python3 <this> [commit]
Prints the record's source hashes compared to the tree, the i686 g++ -MM
include closure of the three translation units the fixture builds (with the
builder's FLAGS and shim) plus the builder and runner, and the closure files in
git diff <record commit> <commit>. Binds when both lists are empty.
"""
import hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path.cwd()
sys.path.insert(0, str(ROOT / 'verification/probe'))
from run_chase_aim_trace import FLAGS  # same flags as build_engine_effects_patch.py

record = json.loads((ROOT / 'verification/results/bottle-X3/engine-effects-patch.json').read_text())
src = record['source']
mismatch = [p for p, h in src['sha256'].items() if hashlib.sha256((ROOT / p).read_bytes()).hexdigest() != h]
shim = 'verification/probe/engine_effects_patch_fixture_shim.h'
units = [('verification/probe/engine_effects_patch_fixture.cpp', []),
         ('src/proxy/engine_effects_patch.cpp', ['-include', shim, '-DX3M_ENGINE_EFFECTS_SHIM']),
         ('src/proxy/engine_patch.cpp', [])]
closure = {'verification/probe/build_engine_effects_patch.py', 'verification/probe/run_engine_effects_patch.py'}
for unit, extra in units:
    out = subprocess.run(['i686-w64-mingw32-g++', *FLAGS, *extra, '-MM', unit], cwd=ROOT, check=True,
                         capture_output=True, text=True).stdout
    for tok in out.replace('\\\n', ' ').split()[1:]:
        p = Path(tok)
        p = (ROOT / p).resolve() if not p.is_absolute() else p.resolve()
        try:
            closure.add(str(p.relative_to(ROOT.resolve())))
        except ValueError:
            pass  # toolchain headers
head = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
changed = set(subprocess.run(['git', 'diff', '--name-only', src['commit'], head], cwd=ROOT, check=True,
                             capture_output=True, text=True).stdout.split())
print(json.dumps({'record_commit': src['commit'], 'hashes': len(src['sha256']), 'hash_mismatch': mismatch,
                  'closure_files': len(closure), 'closure_changed': sorted(closure & changed),
                  'binds': not mismatch and not (closure & changed)}))
