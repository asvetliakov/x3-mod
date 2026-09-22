#!/usr/bin/env python3
"""Cleanup batch 6 hash gate (docs/architecture/cleanup-inventory-2026-09-22.md, item 6).

The five resolve programs the pass still creates must keep the post-exit-reset bytecode: the words embedded in
each kept *_inc.h hash to the manifest's bytecode_sha256, and the manifest itself is byte-identical to the
record at the batch's base commit (git show <base>:<path>). Host only; no Wine, no compiler.
Usage: python3 verification/results/cleanup-batch6-hash-gate.py [base-commit, default 112b6aa9]
"""
import hashlib, json, re, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = sys.argv[1] if len(sys.argv) > 1 else '112b6aa9'
KEPT = {'temporal-resolve-program.json': 'temporal_resolve_program_inc.h',
        'temporal-resolve-thin-program.json': 'temporal_resolve_thin_program_inc.h',
        'temporal-resolve-age-program.json': 'temporal_resolve_age_program_inc.h',
        'temporal-resolve-far-program.json': 'temporal_resolve_far_program_inc.h',
        'temporal-resolve-far-camera-program.json': 'temporal_resolve_far_camera_program_inc.h'}
ok = True
for manifest, header in KEPT.items():
    path = ROOT / 'verification/results' / manifest
    record = json.loads(path.read_text())
    words = [int(w, 16) for w in re.findall(r'0x([0-9a-fA-F]{8})u', (ROOT / 'src/renderer' / header).read_text())]
    embedded = hashlib.sha256(struct.pack('<%dI' % len(words), *words)).hexdigest()
    base = subprocess.run(['git', '-C', str(ROOT), 'show', f'{BASE}:verification/results/{manifest}'], capture_output=True).stdout
    same = base == path.read_bytes()
    match = embedded == record['bytecode_sha256'] and len(words) == record['word_count']
    ok = ok and same and match
    print(f'{manifest} bytecode={record["bytecode_sha256"][:16]} words={len(words)} embedded_matches={int(match)} manifest_unchanged_since_{BASE}={int(same)}')
print('HASH_GATE', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
