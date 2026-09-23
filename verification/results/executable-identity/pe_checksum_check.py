#!/usr/bin/env python3
"""Check exe_identity.pe_checksum against the CheckSum imagehlp stored in the game's PE files.

NTCore 4gb_patch.exe sets IMAGE_FILE_LARGE_ADDRESS_AWARE and then writes the
value of imagehlp!MapFileAndCheckSumW into OptionalHeader.CheckSum (disassembly
of 4gb_patch.exe 0x004010f8-0x00401148). This script confirms the host
reimplementation reproduces every nonzero stored CheckSum in the game directory
and prints the value and SHA-256 4gb_patch.exe would produce for X3AP.exe
(docs/reverse-engineering/executable-identity.md). Read-only.

usage: pe_checksum_check.py [--game DIR]
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'verification' / 'probe'))
import exe_identity  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--game', type=Path, default=exe_identity.DEFAULT_EXE.parent)
    args = parser.parse_args()
    rows = {}
    for path in sorted(args.game.glob('*.dll')) + sorted(args.game.glob('*.exe')):
        data = path.read_bytes()
        try:
            stored = exe_identity.checksum(data)
        except ValueError:
            continue
        if stored:
            rows[path.name] = {'stored': f'{stored:#010x}', 'computed': f'{exe_identity.pe_checksum(data):#010x}',
                               'match': stored == exe_identity.pe_checksum(data)}
    exe = exe_identity.data_of(args.game / 'X3AP.exe')
    patched = exe_identity.with_laa(exe, on=True, new_checksum=exe_identity.pe_checksum(exe_identity.with_laa(exe, on=True)))
    report = {'files_with_checksum': rows, 'all_match': bool(rows) and all(r['match'] for r in rows.values()),
              'x3ap_4gb_patch_checksum': f'{exe_identity.checksum(patched):#010x}',
              'x3ap_4gb_patch_sha256': exe_identity.raw_sha256(patched)}
    print(json.dumps(report, indent=1))
    return 0 if report['all_match'] and report['x3ap_4gb_patch_sha256'] == exe_identity.NTCORE_4GB_SHA256 else 1


if __name__ == '__main__':
    sys.exit(main())
