#!/usr/bin/env python3
"""Offline: is the run216 sun lens pair (and the run223 vs_2_0 lens pair) in each installation's archive?

Uses verification/results/fl-shader-coverage/inventory.py's effective_programs (mount order of
tools/analysis/catalogue_mounts.py: root + addon for AP, root + addon2 for FL; a path in several
catalogues resolves to the last one; selected FL mods under addon2/mods are not mounted). Mayhem on
/mnt/c/X3 is installed as numbered addon catalogues (addon/05..15), so it is part of that run.
Prints derived facts only (ids, models, catalogue and effect names).

usage: lens_pair_inventory.py [--output lens_pair_inventory.json] [NAME=DIR ...]
"""
import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'fl-shader-coverage'))
from inventory import effective_programs  # noqa: E402

PAIRS = {'run216_sun_sm3': ('vs_494fe349b8bc12ec', 'ps_7c83ed50c9894e44'),
         'run223_lens_sm2': ('vs_d5e1c75351ed3f04', 'ps_8360f422de08b5bd')}
DEFAULT = ['ap_orig=/mnt/c/X3AP-orig', 'mayhem_ap=/mnt/c/X3', 'fl=/mnt/c/X3FL']


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, default=HERE / 'lens_pair_inventory.json')
    parser.add_argument('games', nargs='*', default=DEFAULT, metavar='NAME=DIR')
    args = parser.parse_args()
    result = {}
    for spec in args.games:
        name, directory = spec.split('=', 1)
        programs, effects = effective_programs(Path(directory))
        entry = {'directory': directory, 'effects': effects, 'programs': len(programs), 'pairs': {}}
        for pair, ids in PAIRS.items():
            entry['pairs'][pair] = {
                identifier: ({'model': programs[identifier]['model'],
                              'catalogues': sorted(programs[identifier]['catalogues']),
                              'effect_paths': sorted(programs[identifier]['effect_paths'])}
                             if identifier in programs else None)
                for identifier in ids}
        result[name] = entry
        print(name, directory, 'effects=%d programs=%d' % (effects, len(programs)))
        for pair, ids in entry['pairs'].items():
            for identifier, found in ids.items():
                print('  %s %s %s' % (pair, identifier, 'absent' if found is None else
                                      '%s in %s (%d effects: %s)' % (found['model'], ','.join(found['catalogues']),
                                                                    len(found['effect_paths']),
                                                                    ','.join(found['effect_paths'][:4]))))
    args.output.write_text(json.dumps(result, indent=1, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
