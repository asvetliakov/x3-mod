"""Run122 candidate: do the engine-plumes/-ribbons/-effects fixture records bind to a tree?

Usage (from the tree root): python3 <this> [commit]
For each summary record: its source hashes compared to the tree, and the files in
git diff <record commit> <commit> that are neither in the hash list nor docs/,
verification/analysis/ or verification/results/ (the record's own fixture inputs:
fixture .cpp, runners, shaders, proxy and renderer sources). A record binds when
both lists are empty. The engine-effects-patch record uses
verification/results/run121-candidate/engine_effects_patch_binding.py.
"""
import hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path.cwd()
head = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
out = {}
for name in ('plumes', 'ribbons', 'effects'):
    rec = json.loads((ROOT / f'verification/results/bottle-X3/engine-{name}/summary.json').read_text())
    src = rec['source']
    mismatch = [p for p, h in src['sha256'].items() if hashlib.sha256((ROOT / p).read_bytes()).hexdigest() != h]
    changed = subprocess.run(['git', 'diff', '--name-only', src['commit'].replace('-dirty', ''), head], cwd=ROOT,
                             check=True, capture_output=True, text=True).stdout.split()
    outside = [p for p in changed if p not in src['sha256']
               and not p.startswith(('docs/', 'verification/analysis/', 'verification/results/'))]
    out[name] = {'record_commit': src['commit'], 'dirty_paths': src.get('dirty_paths'), 'hashes': len(src['sha256']),
                 'hash_mismatch': mismatch, 'changed_outside_hashes': outside,
                 'passed': rec.get('passed'), 'binds': not mismatch and not outside}
print(json.dumps(out))
