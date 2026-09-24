#!/usr/bin/env python3
"""Restore /tmp/x3-shader-sweep/programs from session capture dumps and check it.

The canonical corpus is the archive extraction (751 programs,
docs/reverse-engineering/shader-sweep.md, "Restoring the local corpus"). This
tool adds, or cross-checks, the `ps_<fnv16>.bin` / `vs_<fnv16>.bin` programs the
DLL dumped into copied session folders (/tmp/x3-bottleX3-runNNN). A file is
accepted only when its name is its FNV-1a 64 hash; a name seen twice must carry
identical bytes, and an existing output file that differs is never overwritten.
The output tree is then compared with the tracked inventory and the pinned
SHA-256s the runners assert. No Wine, no game files are touched.
"""
import argparse
import glob
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAME = re.compile(r'^(ps|vs)_([0-9a-f]{16})\.bin$')
# run_motion_output.py RAW (the assert refuses to run without them).
PINS = {'vs_53a0a641107ed76c.bin': 'bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c',
        'ps_8759c7838bbc86c2.bin': '9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0'}


def fnv1a64(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return f'{value:016x}'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('sessions', nargs='+', help='session directory globs, e.g. "/tmp/x3-bottleX3-run*"')
    parser.add_argument('--output', type=Path, default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--inventory', type=Path, default=ROOT / 'verification/results/shader-sweep-inventory.json')
    parser.add_argument('--dry-run', action='store_true', help='check and count without writing')
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        sys.exit('refusing: output must stay outside the repository (copyrighted bytecode)')

    sessions = sorted({Path(p) for pattern in args.sessions for p in glob.glob(pattern) if Path(p).is_dir()})
    found, errors, bad_name = {}, [], 0
    for session in sessions:
        for path in sorted(session.rglob('[pv]s_*.bin')):
            match = NAME.match(path.name)
            if not match:
                continue
            data = path.read_bytes()
            if fnv1a64(data) != match.group(2):
                bad_name += 1
                errors.append(f'name/FNV mismatch: {path}')
                continue
            if path.name in found and found[path.name][0] != data:
                errors.append(f'differing bytes for {path.name}: {found[path.name][1]} vs {path}')
                continue
            found.setdefault(path.name, (data, path))

    written = identical = conflicts = 0
    if not args.dry_run:
        output.mkdir(parents=True, exist_ok=True)
    for name, (data, source) in sorted(found.items()):
        target = output / name
        if target.exists():
            if target.read_bytes() == data:
                identical += 1
            else:
                conflicts += 1
                errors.append(f'existing {target} differs from {source}; not overwritten')
            continue
        if not args.dry_run:
            shutil.copyfile(source, target)
        written += 1

    present = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
               for p in (output.glob('[pv]s_*.bin') if output.is_dir() else [])}
    if args.dry_run:
        present.update({n: hashlib.sha256(d).hexdigest() for n, (d, _) in found.items() if n not in present})
    inventory = {p['id'] + '.bin': p['sha256'] for p in json.loads(args.inventory.read_text())['programs']}
    restored = sum(1 for n, h in inventory.items() if present.get(n) == h)
    wrong = sorted(n for n, h in inventory.items() if n in present and present[n] != h)
    missing = sorted(n for n in inventory if n not in present)
    session_names = set(found)
    pins = {n: ('ok' if present.get(n) == h else 'missing' if n not in present else 'sha256_mismatch') for n, h in PINS.items()}
    summary = {
        'sessions': len(sessions), 'session_dump_names': len(found), 'written': written,
        'already_identical': identical, 'conflicts': conflicts, 'bad_names': bad_name,
        'output_programs': len(present), 'inventory_programs': len(inventory),
        'inventory_restored': restored, 'inventory_missing': len(missing), 'inventory_sha_mismatch': len(wrong),
        'inventory_in_sessions': len(session_names & set(inventory)),
        'session_not_in_inventory': sorted(session_names - set(inventory)),
        'pins': pins, 'dry_run': args.dry_run}
    print(json.dumps(summary, sort_keys=True))
    for line in errors[:20]:
        print(line, file=sys.stderr)
    return 1 if errors or wrong or any(v != 'ok' for v in pins.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
