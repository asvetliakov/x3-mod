#!/usr/bin/env python3
"""Fog route step A (docs/architecture/fog-gpu-cost.md): a fog fixture summary against the committed one of the base
commit 72645b5e, covering the shader fixture and the production pass fixture. Every figure present in both must be
equal (the shader fixture's versus_host, look_versus_host, filtering, temporal and grid rows, the 11 pass-off look
hashes, the pass fixture's repair / composite / shaft / grid / mote figures), except: timing and provenance keys
(exact suffix _ms, _us, _ns, sha256, timestamp, seconds, _per_second); the pass fixture's worker-scheduling counters,
which differ between two runs of identical code (fill / prepare_cpu / steady / recentre / seam upload bytes, hand-over
frames, state_restorations; printed VARIES); its check and atlas-comparison counts (the committed summary predates
cases); and keys the committed summary does not have (ADDED). Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_a_fixture_identity.py <output>/summary.json"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = '72645b5e'
# Provenance and timing, matched on the last key component by exact suffix (never by substring).
VOLATILE_SUFFIXES = ('_ms', '_us', '_ns', 'sha256', 'timestamp', 'seconds', '_per_second')
SCHEDULING = ('pass_fixture.fill.', 'pass_fixture.prepare_cpu.', 'pass_fixture.steady.', 'pass_fixture.recentre.upload_bytes',
              'pass_fixture.seam_recentres[', 'pass_fixture.handover.frames', 'pass_fixture.state_restorations',
              'pass_fixture.checks', 'pass_fixture.atlas_comparisons')


def volatile(key):
    last = key.split('.')[-1].split('[')[0]
    return last.endswith(VOLATILE_SUFFIXES)


def flatten(value, prefix=''):
    if isinstance(value, dict):
        for key, item in value.items():
            yield from flatten(item, f'{prefix}.{key}' if prefix else key)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from flatten(item, f'{prefix}[{index}]')
    else:
        yield prefix, value


def main():
    new = json.loads(Path(sys.argv[1]).read_text())
    old = json.loads(subprocess.run(['git', '-C', str(ROOT), 'show', f'{BASE}:verification/results/fog-density-shader/summary.json'],
                                    capture_output=True, text=True, check=True).stdout)
    a, b = dict(flatten(old)), dict(flatten(new))
    compared = differing = added = varies = 0
    for key in sorted(set(a) | set(b)):
        if volatile(key):
            continue
        if key not in a:
            added += 1
            if key.startswith('gates.'):
                print(f'ADDED {key}: {b.get(key)!r}')
            continue
        if key.startswith(SCHEDULING):
            varies += 1
            if a.get(key) != b.get(key):
                print(f'VARIES {key}: {a.get(key)!r} -> {b.get(key)!r}')
            continue
        compared += 1
        if a.get(key) != b.get(key):
            differing += 1
            print(f'DIFF {key}: {a.get(key)!r} -> {b.get(key)!r}')
    hashes = new.get('visibility_grid', {}).get('pass_off_hashes')
    same_hashes = hashes is not None and hashes['measured'] == hashes['expected']
    print(f'compared={compared} differing={differing} added={added} scheduling={varies} pass_off_hashes_equal={same_hashes} n_hashes={len(hashes["measured"]) if hashes else 0} '
          f'result={new["result"]} gates={sum(new["gates"].values())}/{len(new["gates"])} pass_fixture_checks {old["pass_fixture"]["checks"]} -> {new["pass_fixture"]["checks"]}')
    return 0 if differing == 0 and same_hashes else 1


if __name__ == '__main__':
    sys.exit(main())
