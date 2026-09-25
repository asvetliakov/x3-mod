#!/usr/bin/env python3
"""Fog fixture reuse for the effects stage checkpoint: none of the fog pass fixture's or the fog route bridge's
recorded dependency paths (verification/results/run85-candidate-qualification.json, the lists the run85 record
reused run84's results on) changed against the merge base, so their run85 results stand (AGENTS.md: reuse unchanged
evidence). Prints the git command, the changed-path count per fixture and the union of paths this checkpoint did
change. Run from the repository root; output kept in reuse_deps_out.txt beside this script."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = sys.argv[1] if len(sys.argv) > 1 else 'main'
record = json.loads((ROOT / 'verification/results/run85-candidate-qualification.json').read_text())
paths = {}
for name in ('fog_pass', 'fog_route_bridge'):
    entry = record[name]
    deps = entry.get('dependency_paths_checked') or entry.get('run84_entry', {}).get('dependency_paths_checked') or []
    paths[name] = deps
changed_all = subprocess.run(['git', 'diff', '--name-only', BASE, '--'], capture_output=True, text=True, cwd=ROOT, check=True).stdout.split()
untracked = subprocess.run(['git', 'ls-files', '--others', '--exclude-standard'], capture_output=True, text=True, cwd=ROOT, check=True).stdout.split()
for name, deps in paths.items():
    command = ['git', 'diff', '--stat', BASE, '--', *deps]
    out = subprocess.run(command, capture_output=True, text=True, cwd=ROOT, check=True).stdout.strip()
    touched = sorted(set(deps) & (set(changed_all) | set(untracked)))
    print('%s: dependency_paths=%d changed=%d %s' % (name, len(deps), len(touched), touched or 'REUSABLE'))
    print('  command: %s (%d paths) -> %s' % (' '.join(command[:5]), len(deps), 'empty' if not out else out.splitlines()[-1]))
print('changed against %s: %d tracked, %d untracked' % (BASE, len(changed_all), len(untracked)))
