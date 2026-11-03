#!/usr/bin/env python3
"""Reruns the executable part of the FL nozzle-scene-window producer (docs/reverse-engineering/fl/nozzle-scene-window.md;
the producer and its contract.json live only in commit 276bd2d62, not on main) against the installed X3FL.exe and
compares the 20 region hashes with that contract. Its source-clause checks describe that branch's motion_output.cpp
and are not rerun. Prints one JSON line.

  git show 276bd2d62:verification/results/fl-port/nozzle-scene-window/produce.py > <dir>/fl-port/nozzle-scene-window/produce.py
  git show 276bd2d62:verification/results/fl-port/nozzle-scene-window/contract.json > <dir>/contract_276.json
  python3 -I verification/results/fl-nozzle-walk/scene_window_rerun.py <produce.py> <contract_276.json> [--exe /mnt/c/X3FL/X3FL.exe]

The producer computes ROOT as its parents[4] (tools/analysis/inspect_x3.py), so place it at that depth in a checkout.
"""
import argparse
import importlib.util
import json


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('producer')
    ap.add_argument('contract')
    ap.add_argument('--exe', default='/mnt/c/X3FL/X3FL.exe')
    args = ap.parse_args()
    spec = importlib.util.spec_from_file_location('nozzle_scene_window', args.producer)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    data = open(args.exe, 'rb').read()
    regions = module.inspect(data)  # asserts the documented SHA-256 of the exe
    regions = regions[0] if isinstance(regions, tuple) else regions
    old = {r['name']: r['sha256'] for r in json.load(open(args.contract))['regions']}
    new = {r['name']: r['sha256'] for r in regions}
    controls = module.mutation_checks(data)
    equal = sum(old.get(k) == v for k, v in new.items())
    print(json.dumps(dict(regions=len(new), contract_regions=len(old), identical=equal, negative_controls=len(controls),
                          passed=equal == len(old) == len(new))))
    return 0 if equal == len(old) == len(new) else 1


if __name__ == '__main__':
    raise SystemExit(main())
