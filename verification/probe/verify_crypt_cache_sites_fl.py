#!/usr/bin/env python3
"""Read-only qualification of the FL crypt_cache rows (src/engine/fl/crypt_cache_sites_fl.h, crypt_cache_fl.cpp) on
X3FL.exe.

docs/reverse-engineering/fl/loading-hooks.md, "Port (2026-11-03)". Checks on the installed exe (default
/mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8...; every value an RVA):

- the crypt windows of loading_windows_fl_inc.h equal the windows regenerated from the exe; every byte / fixup change
  is refused;
- ADVAPI32.dll's named imports put CryptAcquireContextA / CryptImportKey / CryptDestroyKey / CryptReleaseContext at the
  header's four slots; the provider and container strings at their RVAs;
- each of the six gates is a six-byte `call dword ptr [slot]` ending at its return RVA, inside the checker, through its
  operation's slot; the checker pushes delete 0x10 / new keyset 8 with type 1 around the acquires;
- the relocation census of the four slots: acquire five (three checker calls, the sibling's EBX load, its final
  delete), import two, destroy two, release two; every reference is either a gate or the sibling signer's (no third
  user, no data alias), and the sibling's two `call ebx` follow its load;
- AP refusal: no FL crypt window admits the AP exe; AP's six gate VAs do not hold `call [FL slot]` in FL.

usage: verify_crypt_cache_sites_fl.py [--exe X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

import fl_loading_sites_common as c

SITES = c.ROOT / 'src/engine/fl/crypt_cache_sites_fl.h'
COUNTS = {'CryptAcquireContextA': 5, 'CryptImportKey': 2, 'CryptDestroyKey': 2, 'CryptReleaseContext': 2}
SIBLING = {'CryptAcquireContextA': {0x0f7c9e, 0x0f8b3a}, 'CryptImportKey': {0x0f8a02}, 'CryptDestroyKey': {0x0f8b15},
           'CryptReleaseContext': {0x0f8b21}}
AP_GATES = (0x4cac3e, 0x4cac57, 0x4cac85, 0x4cae4d, 0x4cae5a, 0x4cae73)
INDEX = ('CryptDestroyKey', 'CryptReleaseContext', 'CryptImportKey', 'CryptAcquireContextA')


def verify(exe=c.DEFAULT_EXE, sha256=c.FL_SHA256):
    im = c.Image(exe)
    checks, report = {}, {'exe': str(exe), 'sha256': im.sha256}
    checks['identity'] = (sha256 is None or im.sha256 == sha256) and im.pe.base == c.BASE
    c.check_windows(im, 'crypt', checks, report)
    cases, refused = c.mutation_refusals(im, 'crypt')
    report['mutations'] = {'cases': cases, 'refused': refused}
    checks['window_mutations_refused'] = cases > 0 and cases == refused
    text = SITES.read_text()
    pinned = c.header_imports(SITES)
    advapi = c.imports(im, 'ADVAPI32.dll')
    report['slots'] = {k: hex(v) for k, v in sorted(pinned.items())}
    checks['named_import_slots'] = len(pinned) == 4 and all(advapi.get(n) == r for n, r in pinned.items())
    strings = dict(re.findall(r'constexpr char (\w+)\[\] = "([^"]+)";', text))
    srva = {k: int(v, 16) for k, v in re.findall(r'(provider_string_rva|container_string_rva) = (0x[0-9a-f]+)', text)}
    checks['strings'] = (im.read(srva['provider_string_rva'], 43) == strings['provider_string'].encode() + b'\0' and
                         im.read(srva['container_string_rva'], 22) == strings['container_string'].encode() + b'\0')
    gates = [(int(r, 16), INDEX[['destroy_index', 'release_index', 'import_index', 'acquire_index'].index(i)])
             for r, i in re.findall(r'\{(0x[0-9a-f]+), (\w+_index)\}', text)]
    report['gates'] = [hex(r) for r, _ in gates]
    gate_ok = len(gates) == 6
    for ret, name in gates:
        insns = list(im.md.disasm(im.read(ret - 6, 6), c.BASE + ret - 6))
        gate_ok = gate_ok and len(insns) == 1 and insns[0].mnemonic == 'call' and \
            insns[0].op_str == 'dword ptr [0x%x]' % (c.BASE + advapi[name]) and 0x0f8b50 <= ret - 6 < 0x0f8b50 + 0x29e
    checks['gates_call_their_slots'] = gate_ok
    checker, whole = im.insns(0x0f8b50, 0x0f8b50 + 0x29e)
    pushes = [(i.mnemonic, i.op_str) for i in checker if i.mnemonic == 'push' and i.op_str in ('0x10', '8', '1')]
    checks['checker_flags'] = whole and pushes.count(('push', '0x10')) == 2 and pushes.count(('push', '8')) == 1 \
        and pushes.count(('push', '1')) >= 3
    census, ok = {}, True
    gate_calls = {name: {ret - 6 for ret, n in gates if n == name} for name in INDEX}
    for name, expected in COUNTS.items():
        refs = c.slot_references(im, advapi[name])
        census[name] = [hex(r) for r, _, _ in refs]
        at = {r for r, _, _ in refs}
        ok = ok and len(refs) == expected and at == gate_calls[name] | SIBLING[name] and \
            all(m in ('call', 'mov') for _, m, _ in refs) and not c.pointers_outside_text(im, advapi[name])
    report['census'] = census
    checks['reference_census'] = ok
    head, head_ok = im.insns(0x0f7c90, 0x0f7cee)
    calls_ebx = [i.address - c.BASE for i in head if i.mnemonic == 'call' and i.op_str == 'ebx']
    checks['sibling_acquire_via_ebx'] = head_ok and calls_ebx == [0x0f7cca, 0x0f7cdf]
    admitted = c.ap_windows_admitted('crypt')
    report['ap_windows_admitted'] = admitted
    ap_hits = []
    for va in AP_GATES:
        try:
            insns = list(im.md.disasm(im.read(va - c.BASE - 6, 6), va - 6))
        except ValueError:
            continue
        if insns and insns[0].mnemonic == 'call' and any('0x%x' % (c.BASE + s) in insns[0].op_str for s in advapi.values()):
            ap_hits.append(hex(va))
    report['ap_gates_in_fl'] = ap_hits
    checks['ap_addresses_refused'] = admitted == [] and not ap_hits
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
