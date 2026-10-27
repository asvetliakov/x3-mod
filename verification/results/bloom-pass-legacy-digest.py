#!/usr/bin/env python3
"""Print the legacy-case digests test_bloom_pass_fixture pins, for the tree given as argument (default: this repo).

  python3 verification/results/bloom-pass-legacy-digest.py [tree_root]

tree_root may be an extracted older commit (git archive <commit> | tar -x -C dir) to locate the change that moved
the expected-bytes digest. Prints the inputs and expected SHA-256 over the first 24 cases.
"""
import hashlib
import struct
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]).resolve()
sys.path.insert(0, str(root / 'verification/probe'))
import run_bloom_pass as fixture  # noqa: E402

assert Path(fixture.__file__).resolve().is_relative_to(root), fixture.__file__
inputs, expected = hashlib.sha256(), hashlib.sha256()
for c in fixture.make_cases()[:24]:
    inputs.update(struct.pack('<3I2f', c['width'], c['height'], fixture.ref.DECODE_MODES.index(c['mode']),
                              c['strength'], c['sharp']))
    for row in c['image']:
        for pixel in row:
            inputs.update(struct.pack('<4e', *pixel))
    for row in fixture.expected(c):
        for pixel in row:
            expected.update(struct.pack('<3d', *pixel))
print('inputs', inputs.hexdigest())
print('expected', expected.hexdigest())
