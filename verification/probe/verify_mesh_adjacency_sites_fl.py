#!/usr/bin/env python3
"""Read-only qualification of the FL mesh adjacency rows (src/engine/fl/mesh_adjacency_sites_fl.h; the FL branch of
src/proxy/loading_trace.cpp) on X3FL.exe.

docs/reverse-engineering/fl/mesh-adjacency.md, "Port (2026-11-03)". Checks on the installed exe (default
/mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8...; every value an RVA):

- the mesh windows of loading_windows_fl_inc.h equal the windows regenerated from the exe; every change refused;
- the factories are named imports of d3dx9_43.dll at the header's two slots (D3DXCreateMesh, D3DXCleanMesh), and
  X3FL.exe imports nothing from d3dx9_37.dll (AP's import, which loading_trace matches on AP);
- the relocation census: one `call [slot]` each, at 0x0dbbdb and 0x0dcd35 (inside the create / clean windows), no
  data alias; the clean call pushes type 3; the adjacency call writes epsilon 0x358637bd and calls vtable +0x58, the
  fallback +0x50 (slots 22 and 20 of the public ID3DXMesh interface);
- AP refusal: no FL mesh window admits the AP exe; AP's mesh IAT VAs are not FL's d3dx9_43 slots.

usage: verify_mesh_adjacency_sites_fl.py [--exe X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

import fl_loading_sites_common as c

SITES = c.ROOT / 'src/engine/fl/mesh_adjacency_sites_fl.h'
AP_SLOTS = {'D3DXCreateMesh': 0x532360, 'D3DXCleanMesh': 0x53235c}
CALLS = {'D3DXCreateMesh': 0x0dbbdb, 'D3DXCleanMesh': 0x0dcd35}


def verify(exe=c.DEFAULT_EXE, sha256=c.FL_SHA256):
    im = c.Image(exe)
    checks, report = {}, {'exe': str(exe), 'sha256': im.sha256}
    checks['identity'] = (sha256 is None or im.sha256 == sha256) and im.pe.base == c.BASE
    c.check_windows(im, 'mesh', checks, report)
    cases, refused = c.mutation_refusals(im, 'mesh')
    report['mutations'] = {'cases': cases, 'refused': refused}
    checks['window_mutations_refused'] = cases > 0 and cases == refused
    text = SITES.read_text()
    pins = {'D3DXCreateMesh': int(re.search(r'create_slot_rva = (0x[0-9a-f]+)', text).group(1), 16),
            'D3DXCleanMesh': int(re.search(r'clean_slot_rva = (0x[0-9a-f]+)', text).group(1), 16)}
    d3dx = c.imports(im, 'd3dx9_43.dll')
    report['slots'] = {k: hex(v) for k, v in pins.items()}
    report['dlls'] = sorted(im.pe.imports())
    checks['named_import_slots'] = all(d3dx.get(n) == r for n, r in pins.items()) and '"d3dx9_43.dll"' in text
    checks['no_d3dx9_37_import'] = not any(d.lower() == 'd3dx9_37.dll' for d in im.pe.imports())
    census, ok = {}, True
    for name, slot in pins.items():
        refs = c.slot_references(im, slot)
        census[name] = [hex(r) for r, _, _ in refs]
        ok = ok and [r for r, m, _ in refs if m == 'call'] == [CALLS[name]] and len(refs) == 1 and \
            not c.pointers_outside_text(im, slot)
    report['census'] = census
    checks['reference_census'] = ok
    clean, clean_ok = im.insns(0x0dcd2a, 0x0dcd3b)
    adj, adj_ok = im.insns(0x0dcc40, 0x0dcc52)
    reps, reps_ok = im.insns(0x0dcc98, 0x0dcca4)
    checks['clean_type_3'] = clean_ok and ('push', '3') in [(i.mnemonic, i.op_str) for i in clean]
    checks['adjacency_abi'] = adj_ok and reps_ok and \
        ('mov', 'dword ptr [esp], 0x358637bd') in [(i.mnemonic, i.op_str) for i in adj] and \
        adj[-1].op_str == 'dword ptr [edx + 0x58]' and reps[-1].op_str == 'dword ptr [ecx + 0x50]'
    admitted = c.ap_windows_admitted('mesh')
    report['ap_windows_admitted'] = admitted
    checks['ap_addresses_refused'] = admitted == [] and all(va - c.BASE not in d3dx.values() for va in AP_SLOTS.values())
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
