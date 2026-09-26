#!/usr/bin/env python3
"""Compare a fresh fog_density_shader_run.py check output with the committed record (host only).

Every 64-hex digest in the committed verification/results/fog-density-shader/{summary,s2,q4}.json except the provenance
digests (rebuilt executables, exporter) is compared with the same key path in the fresh output directory's files;
prints the count of equal and differing digests per file. Used for the 2026-09-26 baking of the fog look and mote constants (the baked values
equal the previous defaults, so every accepted image hash must be identical).

    python3 verification/results/logging-tiers/fog_hashes_compare.py <output dir of fog_density_shader_run.py>
"""
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
HEX = re.compile(r'^[0-9a-f]{64}$')


def digests(node, path=()):
    if isinstance(node, dict):
        for key, value in node.items():
            yield from digests(value, path + (key,))
    elif isinstance(node, list):
        for index, value in enumerate(node):
            yield from digests(value, path + (index,))
    elif isinstance(node, str) and HEX.match(node):
        yield path, node


def main():
    fresh_dir = Path(sys.argv[1])
    total_equal = total_diff = 0
    for name in ('summary.json', 's2.json', 'q4.json'):
        committed_path = ROOT / 'verification/results/fog-density-shader' / name
        fresh_path = fresh_dir / name
        if not committed_path.is_file() or not fresh_path.is_file():
            print(name, 'missing', committed_path.is_file(), fresh_path.is_file())
            continue
        committed = dict(digests(json.loads(committed_path.read_text())))
        fresh = dict(digests(json.loads(fresh_path.read_text())))
        # Provenance digests (the rebuilt executables, the exporter) are expected to differ; every other digest must not.
        image_keys = [k for k in committed if not any(isinstance(p, str) and ('executable' in p or 'exporter' in p) for p in k)]
        equal = [k for k in image_keys if fresh.get(k) == committed[k]]
        differ = [k for k in image_keys if fresh.get(k) != committed[k]]
        total_equal += len(equal)
        total_diff += len(differ)
        print(name, 'image digests equal', len(equal), 'differ', len(differ), [('/'.join(map(str, k)), committed[k][:12], (fresh.get(k) or '-')[:12]) for k in differ][:10])
    print('total equal', total_equal, 'differ', total_diff)
    return 1 if total_diff else 0


if __name__ == '__main__':
    sys.exit(main())
