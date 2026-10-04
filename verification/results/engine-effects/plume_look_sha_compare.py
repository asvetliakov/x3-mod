#!/usr/bin/env python3
"""sha256 of each look image (PNG) in two dumps, same / DIFF per image.

Usage: python3 plume_look_sha_compare.py BEFORE_DIR AFTER_DIR
(2026-10-04, the disc's distance law after Run 127: BEFORE = the tracked look-images at d562700e copied to a scratch
directory, AFTER = this tree's re-dump; plume_look_sha_discfar_out.txt.)
"""
import hashlib
import sys
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


before, after = Path(sys.argv[1]), Path(sys.argv[2])
same = 0
names = sorted(p.name for p in after.glob('*.png'))
for name in names:
    a, b = sha(before / name), sha(after / name)
    same += a == b
    print(f"{'same' if a == b else 'DIFF'} {a[:16]} {b[:16]} {name}")
print(f"{same} of {len(names)} identical")
