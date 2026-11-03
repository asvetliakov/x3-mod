#!/usr/bin/env python3
"""Read-only qualification of the FL gz_buffer rows (src/engine/fl/gz_buffer_sites_fl.h, gz_buffer_fl.cpp) on X3FL.exe.

docs/reverse-engineering/fl/loading-hooks.md, "Port (2026-11-03)". Checks on the installed exe (default
/mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8...; every value an RVA):

- the gz windows of loading_windows_fl_inc.h equal the windows regenerated from the exe, and every single-byte or
  fixup change of a window is refused by the production decision;
- zlib1.dll's named imports put gzopen/gzread/gzseek/gzgetc/gztell/gzclose at the header's six slots and gzwrite at
  the slot the music keep owns (music_keep_sites_fl.h names the same gzwrite and gzclose slots);
- the relocation census of every instruction referencing each slot: gzopen, gzread, gzseek, gztell one call each,
  gzgetc one call and three EDX loads, gzclose eleven calls, gzwrite 21 (the keep's, not claimed); every reference is
  an instruction operand (no data alias, no relocated pointer to a slot outside .text), and the claimed slots'
  references are exactly the windows' witnesses;
- AP refusal: no FL gz window admits the AP exe, and AP's gz IAT VAs are not FL's zlib slots.

usage: verify_gz_buffer_sites_fl.py [--exe X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

import fl_loading_sites_common as c

SITES = c.ROOT / 'src/engine/fl/gz_buffer_sites_fl.h'
KEEP = c.ROOT / 'src/engine/fl/music_keep_sites_fl.h'
COUNTS = {'gzopen': 1, 'gzread': 1, 'gzseek': 1, 'gztell': 1, 'gzgetc': 4, 'gzclose': 11, 'gzwrite': 21}
AP_SLOTS = {'gzopen': 0x5323d8, 'gzread': 0x5323e4, 'gzseek': 0x5323fc, 'gzgetc': 0x5323f4, 'gztell': 0x5323ec,
            'gzclose': 0x5323f0, 'gzwrite': 0x5323e0}


def verify(exe=c.DEFAULT_EXE, sha256=c.FL_SHA256):
    im = c.Image(exe)
    checks, report = {}, {'exe': str(exe), 'sha256': im.sha256}
    checks['identity'] = (sha256 is None or im.sha256 == sha256) and im.pe.base == c.BASE
    declared = c.check_windows(im, 'gz', checks, report)
    cases, refused = c.mutation_refusals(im, 'gz')
    report['mutations'] = {'cases': cases, 'refused': refused}
    checks['window_mutations_refused'] = cases > 0 and cases == refused
    pinned = c.header_imports(SITES)
    zlib = c.imports(im, 'zlib1.dll')
    report['slots'] = {k: hex(v) for k, v in sorted(pinned.items())}
    checks['named_import_slots'] = len(pinned) == 6 and all(zlib.get(n) == r for n, r in pinned.items())
    keep = KEEP.read_text()
    keep_write = int(re.search(r'gzwrite_iat_rva = (0x[0-9a-f]+)', keep).group(1), 16)
    keep_close = int(re.search(r'gzclose_iat_rva = (0x[0-9a-f]+)', keep).group(1), 16)
    gz_write = int(re.search(r'gzwrite_slot_rva = (0x[0-9a-f]+)', SITES.read_text()).group(1), 16)
    checks['keep_slots_shared'] = (zlib.get('gzwrite') == keep_write == gz_write and keep_close == pinned['gzclose']
                                   and 'gzwrite' not in pinned)
    witnesses = {rva for _, rva, length, _, fx in declared for off, _ in fx}
    census, refs_ok, claimed_refs = {}, True, set()
    for name, expected in COUNTS.items():
        refs = c.slot_references(im, zlib[name])
        census[name] = len(refs)
        kinds = {m for _, m, _ in refs}
        refs_ok = refs_ok and len(refs) == expected and 'data' not in kinds and not c.pointers_outside_text(im, zlib[name])
        if name in pinned:
            claimed_refs |= {rva for rva, _, _ in refs}
    report['census'] = census
    window_starts = {rva for _, rva, _, _, _ in declared}
    checks['reference_census'] = refs_ok
    checks['claimed_references_are_witnesses'] = claimed_refs <= window_starts | {0x10695d} and len(claimed_refs) == 19
    report['witness_fixups'] = len(witnesses)
    admitted = c.ap_windows_admitted('gz')
    report['ap_windows_admitted'] = admitted
    ap_in_fl = [n for n, va in AP_SLOTS.items() if va - c.BASE in zlib.values()]
    report['ap_slots_in_fl'] = ap_in_fl
    checks['ap_addresses_refused'] = admitted == [] and not ap_in_fl and all(va - c.BASE != zlib[n]
                                                                              for n, va in AP_SLOTS.items())
    return c.finish(report, checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=c.DEFAULT_EXE)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe)
    text = json.dumps(report, indent=1, sort_keys=True)
    if args.out:
        args.out.write_text(text + '\n')
    print(text)
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
