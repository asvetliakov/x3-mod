"""Compare the motion-output summary against the committed one (HEAD) per case, setting aside
fields that change with every build or run (paths, binary hashes, timings).
Usage: compare_motion_output.py [HEAD summary copy] [new summary]"""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
SUMMARY = 'verification/results/bottle-X3/motion-output-summary.json'
VOLATILE = {'directory', 'dll_sha256', 'exe_sha256', 'trace_sha256'}


def volatile(key):
    return key in VOLATILE or key == 'us' or '_us' in key or '_ns' in key or key.endswith(('_ms', '_seconds')) or 'timing' in key or 'sha256' in key


def paths(a, b, prefix=''):
    # Key paths whose values differ, recursing into dicts and skipping volatile keys at every depth.
    if isinstance(a, dict) and isinstance(b, dict):
        out = []
        for k in sorted(set(a) | set(b)):
            if not volatile(k):
                out += paths(a.get(k), b.get(k), prefix + '/' + k)
        return out
    return [] if a == b else [prefix]


old = json.loads(Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else subprocess.run(['git', 'show', 'HEAD:' + SUMMARY], cwd=ROOT, capture_output=True, text=True, check=True).stdout)
new = json.loads((Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / SUMMARY).read_text())
print('passed old=%s new=%s' % (old['passed'], new['passed']))
oc, nc = old['cases'], new['cases']
print('cases old=%d new=%d missing=%s added=%s' % (len(oc), len(nc), sorted(set(oc) - set(nc)), sorted(set(nc) - set(oc))))
print('checks old=%d new=%d' % (sum(c.get('checks', 0) for c in oc.values()), sum(c.get('checks', 0) for c in nc.values())))
diffs = {}
for name in sorted(set(oc) & set(nc)):
    changed = paths(oc[name], nc[name])
    if changed:
        diffs[name] = changed
print('cases with differing stable fields: %d' % len(diffs))
for name, keys in list(diffs.items())[:20]:
    print(' ', name, keys[:8])
