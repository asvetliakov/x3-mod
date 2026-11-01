#!/usr/bin/env python3
"""Cross-check the part 4b rows against an independent derivation.

The archived fl-codex commit 636258da carried FL material rows produced by a
different analyzer (lane-precise DAG over CTAB tags; tools/analysis/
inspect_fl_materials.py there, never merged). This compares, field by field,
the rows of linear-materials-fl.json (inspect_fl_linear_materials.py) with that
commit's linear_fl_{pixel,xt,seed}_profiles_inc.h and the FL hull rows of its
linear_emission.cpp, read with `git show`. Fields absent from either side
(fl-codex's XT reflection role; this change's original_only flag) are skipped.
Writes codex-crosscheck.json beside this script (counts and differing names only).

usage: crosscheck_codex.py [--commit 636258da]
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def show(commit, path):
    return subprocess.run(['git', '-C', str(ROOT), 'show', f'{commit}:{path}'], capture_output=True, text=True,
                          check=True).stdout


def numbers(text):
    text = text.replace('true', '1').replace('false', '0')
    return [int(x, 0) for x in re.findall(r'0x[0-9a-f]+(?=ull)|\b\d+\b', text)]


def rows(text):
    starts = [m.start() for m in re.finditer(r'\{\s*0x[0-9a-f]{16}ull', text)]
    return {re.search(r'0x([0-9a-f]{16})ull', text[b:e]).group(1): numbers(text[b:e])
            for b, e in zip(starts, starts[1:] + [len(text)])}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--commit', default='636258da')
    arguments = parser.parse_args()
    report = json.loads((HERE / 'linear-materials-fl.json').read_text())
    mine = {p['ps']: p for p in report['programs']}
    differing = {'pixel': [], 'xt': [], 'seed': [], 'hull': []}
    compared = {key: 0 for key in differing}
    for key, theirs in rows(show(arguments.commit, 'src/renderer/linear_fl_pixel_profiles_inc.h')).items():
        p, r = mine[key], mine[key]['row']
        ours = [int(key, 16), p['words'], *r['textures'], r['affine_end'], r['clamp'], r['final_rgb'],
                r['clamp_temporary'], r['light0'], r['light1'], *r['color_source'], *r['rgb'], int(r['bump']),
                r['depth_texcoord_override'], 0, 0, 0, r['glow'], r['affine_first'], r['temporary_base']]
        compared['pixel'] += 1
        if ours != theirs:
            differing['pixel'].append(key)
    for key, theirs in rows(show(arguments.commit, 'src/renderer/linear_fl_xt_profiles_inc.h')).items():
        p, r = mine[key], mine[key]['row']
        ours = [int(key, 16), p['words'], int(r['bump']), 0, int(r['terra']), r['base_at'], r['base_reg'],
                r['final_rgb'], r['alpha'], *r['textures'], *r['texture_registers'],
                *[x for use in r['lights'] for x in use], *[x for use in r['palette'] for x in use], *r['clamps'],
                *[x for use in r['scalar'] for x in use], *r['rgb'], r['light0'], r['light1'], r['palette_first']]
        compared['xt'] += 1
        # theirs ends with the reflection role and the temporary base.
        if ours != theirs[:-2] or theirs[-1] != r['temporary_base']:
            differing['xt'].append(key)
    for key, theirs in rows(show(arguments.commit, 'src/renderer/linear_fl_seed_profiles_inc.h')).items():
        j = mine[key]['join']
        compared['seed'] += 1
        if theirs[1:] != [j['at'], j['lanes'], int(j['product'])]:
            differing['seed'].append(key)
    emission = show(arguments.commit, 'src/renderer/linear_emission.cpp')
    block = emission[emission.index('// FL standard/XT variants'):]
    block = block[:block.index('};')]
    theirs_hull = rows(block)
    for key, theirs in theirs_hull.items():
        h = mine[key]['hull_emitter']
        compared['hull'] += 1
        if not h or theirs[1:] != [mine[key]['words'], h['definition'], h['emission'], int(h['modulated'])]:
            differing['hull'].append(key)
    ours_hull = sorted(k for k, p in mine.items() if p['hull_emitter'])
    result = {'producer': str(Path(__file__).relative_to(ROOT)), 'commit': arguments.commit,
              'rows_compared': compared, 'differing': differing,
              'hull_program_sets_equal': ours_hull == sorted(theirs_hull),
              'all_equal': not any(differing.values()) and ours_hull == sorted(theirs_hull)}
    (HERE / 'codex-crosscheck.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result))
    return 0 if result['all_equal'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
