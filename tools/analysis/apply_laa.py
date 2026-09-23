#!/usr/bin/env python3
"""Report, and with --apply set, IMAGE_FILE_LARGE_ADDRESS_AWARE on X3AP.exe.

Reports the bit, the raw SHA-256 against the known list, the SHA-256 the file
would have with the bit set and with it cleared, and whether the file is the
known image (structure and global anchors, verification/probe/exe_identity.py).
With --apply, and only when the bit is clear, the game is not running
(verification/probe/game_guard.py) and the file is the known image: the
original is kept as <exe>.x3m-pre-laa (never overwritten), the bit is set in a
temporary copy that is re-read and checked, and the copy replaces the file.
Only the Characteristics word changes; the PE CheckSum is left as it is (the
Windows loader does not check it for an EXE). NTCore's 4gb_patch.exe also
rewrites CheckSum, so its output hashes differently
(docs/reverse-engineering/executable-identity.md).

usage: apply_laa.py [--exe PATH] [--apply]   (exit 0 = done or nothing to do)
"""
import argparse
import json
import os
import shutil
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
import exe_identity  # noqa: E402

BACKUP_SUFFIX = '.x3m-pre-laa'


def running_game():
    from game_guard import game_running
    return game_running()


def report(data):
    info = exe_identity.info(data)
    return {**info, 'identity_ok': exe_identity.identity_ok(data),
            'sha256_with_laa': exe_identity.raw_sha256(exe_identity.with_laa(data, on=True)),
            'sha256_without_laa': exe_identity.raw_sha256(exe_identity.with_laa(data, on=False))}


def apply(exe, guard=running_game):
    """Set the bit; returns the post-state record. Raises RuntimeError on refusal."""
    data = exe.read_bytes()
    before = report(data)
    if before['laa']:
        return {'action': 'none', 'reason': 'already_set', 'before': before}
    if not before['identity_ok']:
        raise RuntimeError('not the known X3AP.exe image (structure/anchors); refusing to modify it')
    lines = guard()
    if lines:
        raise RuntimeError('X3AP.exe is running; close the game first: ' + '; '.join(lines))
    backup = exe.with_name(exe.name + BACKUP_SUFFIX)
    if backup.exists():
        if backup.read_bytes() != data:
            raise RuntimeError(f'{backup} exists and differs from {exe}; refusing to overwrite it')
    else:
        temporary = backup.with_name(backup.name + '.tmp')
        temporary.write_bytes(data)
        shutil.copystat(exe, temporary)
        os.replace(temporary, backup)
    patched = exe_identity.with_laa(data, on=True)
    temporary = exe.with_name(exe.name + '.x3m-laa.tmp')
    temporary.write_bytes(patched)
    shutil.copymode(exe, temporary)
    if temporary.read_bytes() != patched or not exe_identity.laa(temporary):
        temporary.unlink()
        raise RuntimeError('temporary copy did not verify')
    os.replace(temporary, exe)
    after = report(exe.read_bytes())
    if not after['laa'] or after['sha256'] != before['sha256_with_laa']:
        raise RuntimeError(f'post-write check failed; restore from {backup}')
    return {'action': 'set', 'backup': str(backup), 'before': before, 'after': after}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=exe_identity.DEFAULT_EXE)
    parser.add_argument('--apply', action='store_true', help='set the bit (default: report only)')
    args = parser.parse_args()
    if not args.exe.is_file():
        print(json.dumps({'error': f'{args.exe} not found'}))
        return 2
    if not args.apply:
        print(json.dumps({'action': 'report', 'exe': str(args.exe), **report(args.exe.read_bytes())}, indent=1))
        return 0
    try:
        print(json.dumps({'exe': str(args.exe), **apply(args.exe)}, indent=1))
    except RuntimeError as error:
        print(json.dumps({'exe': str(args.exe), 'action': 'refused', 'reason': str(error)}), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
