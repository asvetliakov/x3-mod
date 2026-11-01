#!/usr/bin/env python3
"""AP parity of part 4b: every public material-variant API, byte for byte.

Builds ap_identity.cpp twice with the host compiler, once against the base
commit's src/renderer (git archive into the scratch directory) and once against
the working tree, runs both over every program of the AP corpus (and the AP
motion result's pairs for the pair-contract lookups), and requires identical
digests per program. A second pass over the FL corpus records which programs
the change moves there (expected: exactly the 34 FL-only pixel programs).
Writes ap-identity.json beside this script (counts and names only; no bytes).

usage: ap_identity.py <scratch dir> [--base 531d0f70] [--ap DIR] [--fl DIR]
"""
import argparse
import json
from pathlib import Path
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCES = ('linear_material.cpp', 'material_motion.cpp', 'linear_emission.cpp')


def build(renderer, executable):
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(renderer),
                    str(HERE / 'ap_identity.cpp'), *(str(renderer / name) for name in SOURCES), '-o', str(executable)],
                   check=True)


def run(executable, corpus, pairs):
    names = sorted(path.stem for path in corpus.glob('*.bin') if path.stem[:3] in ('ps_', 'vs_'))
    lines = []
    for begin in range(0, len(names), 200):
        result = subprocess.run([str(executable), str(corpus), str(pairs), *names[begin:begin + 200]],
                                capture_output=True, text=True, check=True)
        lines += [line for line in result.stdout.splitlines() if not line.startswith('pairs ') or begin == 0]
    return dict(line.split(' ', 1) for line in lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('scratch', type=Path)
    parser.add_argument('--base', default='531d0f70')
    parser.add_argument('--ap', type=Path, default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--fl', type=Path, default=Path('/tmp/x3-fl-shader-sweep/programs'))
    arguments = parser.parse_args()
    scratch = arguments.scratch.resolve()
    if ROOT in scratch.parents or scratch == ROOT:
        raise SystemExit('keep the scratch directory outside the repository')
    scratch.mkdir(parents=True, exist_ok=True)
    base = scratch / 'base'
    subprocess.run(['rm', '-rf', str(base)], check=True)
    base.mkdir()
    archive = subprocess.run(['git', '-C', str(ROOT), 'archive', arguments.base, 'src/renderer'],
                             capture_output=True, check=True).stdout
    subprocess.run(['tar', '-x', '-C', str(base)], input=archive, check=True)
    commit = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--short', arguments.base], capture_output=True,
                            text=True, check=True).stdout.strip()
    build(base / 'src/renderer', scratch / 'base_probe')
    build(ROOT / 'src/renderer', scratch / 'tree_probe')
    pairs = scratch / 'pairs.txt'
    profiles = json.loads((ROOT / 'verification/results/motion-output-profiles.json').read_text())
    fl = json.loads((HERE / 'motion-output-profiles-fl.json').read_text())
    pairs.write_text(''.join(f"{p['vs']} {p['ps']}\n" for p in profiles['pairs']))
    started = time.monotonic()
    before, after = run(scratch / 'base_probe', arguments.ap, pairs), run(scratch / 'tree_probe', arguments.ap, pairs)
    elapsed = time.monotonic() - started
    changed = sorted(name for name in before if before[name] != after.get(name))
    fl_before, fl_after = run(scratch / 'base_probe', arguments.fl, pairs), run(scratch / 'tree_probe', arguments.fl, pairs)
    fl_changed = sorted(name for name in fl_before if fl_before[name] != fl_after.get(name) and name != 'pairs')
    expected = sorted('ps_' + p['ps'] for p in fl['pairs'])
    result = {
        'producer': str(Path(__file__).relative_to(ROOT)),
        'probe': 'ap_identity.cpp: digests of every original-shading form (7 kinds x engine x dynamic x depth) and '
                 'its strength twin, the converted pixel/vertex/XT-default variants (fill 0 and 0.05), the stage '
                 'lookups, the HDR hull gain (clamps 0, 0.5, 1) and the pair contracts of the AP motion result',
        'base_commit': commit,
        'ap_corpus_programs': len(before) - 1, 'ap_pairs': len(profiles['pairs']),
        'ap_pairs_identical': before['pairs'] == after['pairs'],
        'ap_programs_identical': len(before) - 1 - len([n for n in changed if n != 'pairs']),
        'ap_programs_changed': [n for n in changed if n != 'pairs'],
        'fl_corpus_programs': len(fl_before) - 1,
        'fl_programs_changed': len(fl_changed),
        'fl_changed_are_the_34_fl_only_programs': fl_changed == expected,
        'ap_run_seconds': round(elapsed, 1),
    }
    (HERE / 'ap-identity.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result, indent=1))
    return 0 if not changed and result['fl_changed_are_the_34_fl_only_programs'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
