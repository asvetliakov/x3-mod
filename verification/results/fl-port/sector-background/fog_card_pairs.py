#!/usr/bin/env python3
"""The fog-card program pairs of X3FL.exe's resolved effect set, from the offline shader inventory.

Uses verification/results/fl-shader-coverage/inventory.py's effective_programs() (the proxy's program key: FNV-1a 64
over the complete token stream; FL root + addon2 mount order) over /mnt/c/X3FL and /mnt/c/X3AP-orig, and prints for
each fog-card program id its model, DWORD count, the effect basenames using it and whether AP's resolved set has it,
then the VS/PS pairings that occur together in one nebulafog* effect. Derived facts only; no shader words.
Never from log rows (docs/architecture/farnhams-legacy-sector.md, "Fog cards").

usage: fog_card_pairs.py [--fl DIR] [--ap DIR]
"""
import argparse
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location('fl_inventory', ROOT / 'verification/results/fl-shader-coverage/inventory.py')
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)

PROGRAMS = ('vs_7b6393fe2d3e1d85', 'ps_f7e0b6647a3bfa62', 'vs_ececc28e00592d35', 'ps_214fc10d3672ab92')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fl', type=Path, default=Path('/mnt/c/X3FL'))
    parser.add_argument('--ap', type=Path, default=Path('/mnt/c/X3AP-orig'))
    arguments = parser.parse_args()
    fl, fl_effects = inventory.effective_programs(arguments.fl)
    ap, ap_effects = inventory.effective_programs(arguments.ap) if arguments.ap.is_dir() else ({}, 0)
    print(f'effects fl={fl_effects} ap={ap_effects}')
    for identifier in PROGRAMS:
        record = fl.get(identifier)
        if not record:
            print(f'{identifier} fl=absent ap={"present" if identifier in ap else "absent"}')
            continue
        print(f"{identifier} fl=present model={record['model']} dwords={record['dwords']} "
              f"basenames={sorted(record['basenames'])} paths={len(record['effect_paths'])} "
              f"ap={'present' if identifier in ap else 'absent'}")
    pairs = {}
    for vs in (p for p in PROGRAMS if p.startswith('vs_')):
        for ps in (p for p in PROGRAMS if p.startswith('ps_')):
            if vs in fl and ps in fl:
                shared = fl[vs]['effect_paths'] & fl[ps]['effect_paths']
                if shared:
                    pairs[vs, ps] = sorted({Path(p).stem for p in shared})
                    print(f'pair {vs[3:]}/{ps[3:]} effects={len(shared)} basenames={pairs[vs, ps]}')
    return 0 if len(pairs) == 2 else 1


if __name__ == '__main__':
    sys.exit(main())
