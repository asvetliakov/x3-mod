#!/usr/bin/env python3
"""Read-only qualification of the two partial-sun-occlusion call sites against the installed X3AP.exe.

Sites and claims of src/proxy/sun_occlusion_core.h (docs/reverse-engineering/lens-flare-visibility.md,
sections 12 and 16): the flare probe's call 0x00471630 -> 0x00488720 and the lens traversal's call
0x00472491 -> 0x0047e6e0. Verified: the executable's SHA-256; the 21 whole-instruction context bytes
around each call exactly as the header pins them, decoded gap-free by objdump with the call on an
instruction boundary; both rel32 targets; the probe's prologue with its three early tests (148
bytes, the FNV-1a the DLL compares) and their decoded shape (test/jne, four signed compares with
jl/jg to the `xor eax,eax` exit, test/jne back to the `mov eax,1` exit); the traversal's entry;
the exact caller sets of both callees from a raw E8/E9 scan of .text; no direct branch (decoded
rel8/rel32 inside the two owning routines, raw rel32 anywhere in .text) into the interior of
either call; no 32-bit absolute reference to a byte of either call anywhere in the file; and
disjointness from every other claimed site under src/proxy, except the one documented conflict
(X3M_SUBMIT_PHASES' sort_return_b stamp at 0x00472490, refused by name at install). No Wine, no launch.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROXY = ROOT / 'src/proxy'
HEADER = PROXY / 'sun_occlusion_core.h'
DEFAULT_EXE = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'
EXPECTED_SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
OBJDUMP = 'i686-w64-mingw32-objdump'
TEXT_BASE, TEXT_OFFSET, TEXT_SIZE = 0x401000, 0x400, 0x130630
PROBE_SITE, PROBE_TARGET, LENS_SITE, LENS_TARGET = 0x471630, 0x488720, 0x472491, 0x47e6e0
PROBE_CONTEXT = (0x47162a, '8b4c24145156e8eb70010083c40885c07403896e30')
LENS_CONTEXT = (0x472490, '56e84ac200008b0d188560008b810463000083c404')
LENS_ENTRY = 'a11885600053558b6c240c'
GATES = (0x488720, 0x94, 0x0e5f40888a926996)
PROBE_CALLERS, LENS_CALLERS = {0x471630}, {0x4722b5, 0x472491, 0x47e8f6}
OWNERS = ((0x4715d0, 0x471660), (0x471f50, 0x47260c))   # the record loop and the frame routine
KNOWN_CONFLICT = 'submit_phase_sort_return_b'
GATE_SHAPE = [(0x488742, 'test'), (0x488752, 'jne'), (0x488754, 'mov'), (0x488772, 'cmp'), (0x488774, 'jl'), (0x488780, 'cmp'), (0x488782, 'jg'),
              (0x488790, 'cmp'), (0x488796, 'jl'), (0x48879c, 'cmp'), (0x4887a2, 'jg'), (0x4887a8, 'test'), (0x4887b2, 'jne')]


def fnv1a(data):
    h = 0xcbf29ce484222325
    for c in data:
        h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return h


def decode(exe, start, stop):
    text = subprocess.run([OBJDUMP, '-d', '-M', 'intel', '--insn-width=16', f'--start-address={start:#x}', f'--stop-address={stop:#x}', str(exe)],
                          check=True, capture_output=True, text=True, timeout=60).stdout
    rows = []
    for line in text.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\t((?:[0-9a-f]{2} )+)\s*\t?([a-z][a-z0-9]*)?\s*(.*)$', line)
        if m and m.group(3):
            raw = bytes.fromhex(m.group(2).replace(' ', ''))
            rows.append((int(m.group(1), 16), len(raw), m.group(3), m.group(4).strip()))
    return rows


def verify(exe):
    data = Path(exe).read_bytes()
    at = lambda va, n: data[va - TEXT_BASE + TEXT_OFFSET: va - TEXT_BASE + TEXT_OFFSET + n]
    checks = {'sha256': hashlib.sha256(data).hexdigest() == EXPECTED_SHA256}
    checks['probe_context'] = at(PROBE_CONTEXT[0], 21).hex() == PROBE_CONTEXT[1]
    checks['lens_context'] = at(LENS_CONTEXT[0], 21).hex() == LENS_CONTEXT[1]
    rel = lambda site: site + 5 + struct.unpack('<i', at(site + 1, 4))[0]
    checks['targets'] = at(PROBE_SITE, 1) == b'\xe8' and rel(PROBE_SITE) == PROBE_TARGET and at(LENS_SITE, 1) == b'\xe8' and rel(LENS_SITE) == LENS_TARGET
    checks['probe_gates_fnv1a'] = fnv1a(at(GATES[0], GATES[1])) == GATES[2]
    checks['lens_entry'] = at(LENS_TARGET, len(LENS_ENTRY) // 2).hex() == LENS_ENTRY
    # Decoded, gap-free: the calls are whole instructions and the gates have the shape the override replicates.
    interiors = set(range(PROBE_SITE + 1, PROBE_SITE + 5)) | set(range(LENS_SITE + 1, LENS_SITE + 5))
    branches_in = []
    boundaries = set()
    for start, stop in OWNERS:
        rows = decode(exe, start, stop)
        cursor = start
        for va, length, mnemonic, operands in rows:
            if va != cursor:
                checks['gap_free'] = False
            cursor = va + length
            boundaries.add(va)
            target = re.fullmatch(r'0x([0-9a-f]+)', operands)
            if target and (mnemonic == 'call' or mnemonic.startswith('j')) and int(target.group(1), 16) in interiors:
                branches_in.append(hex(va))
    checks.setdefault('gap_free', True)
    checks['instruction_boundaries'] = PROBE_SITE in boundaries and PROBE_SITE + 5 in boundaries and LENS_SITE in boundaries and LENS_SITE + 5 in boundaries
    gate_rows = {va: mnemonic for va, _, mnemonic, _ in decode(exe, GATES[0], GATES[0] + GATES[1])}
    checks['gate_shape'] = all(gate_rows.get(va) == mnemonic for va, mnemonic in GATE_SHAPE)
    exits = {va: (m, o) for va, _, m, o in decode(exe, 0x488a5a, 0x488a63)}
    checks['rect_exit_returns_zero'] = exits.get(0x488a5c) == ('xor', 'eax,eax') and exits.get(0x488a62, ('', ''))[0] == 'ret'
    # Raw scans over .text: callers, and rel32 transfers into a call's interior.
    text = data[TEXT_OFFSET:TEXT_OFFSET + TEXT_SIZE]
    callers = {PROBE_TARGET: set(), LENS_TARGET: set()}
    for i in range(len(text) - 5):
        op = text[i]
        if op in (0xe8, 0xe9) or (op == 0x0f and 0x80 <= text[i + 1] <= 0x8f):
            skip = 2 if op == 0x0f else 1
            target = TEXT_BASE + i + skip + 4 + struct.unpack_from('<i', text, i + skip)[0]
            if op == 0xe8 and target in callers:
                callers[target].add(TEXT_BASE + i)
            if target in interiors:
                branches_in.append(hex(TEXT_BASE + i))
    # A raw scan also matches E8 bytes inside other instructions; the decoded caller set must be a subset and the raw set exact here.
    checks['probe_callers'] = callers[PROBE_TARGET] == PROBE_CALLERS
    checks['lens_callers'] = callers[LENS_TARGET] == LENS_CALLERS
    checks['no_branch_into_interior'] = not branches_in
    spans = [(PROBE_SITE, PROBE_SITE + 5), (LENS_SITE, LENS_SITE + 5)]
    references = [hex(i) for a, b in spans for va in range(a, b) for i in [data.find(struct.pack('<I', va))] if i >= 0]
    checks['no_absolute_reference'] = not references
    # Every other claim under src/proxy: SiteSpec rows and *_site_va call sites.
    conflicts = []
    for path in sorted(PROXY.glob('*')):
        if path.suffix not in ('.h', '.cpp') or path.name.startswith('sun_occlusion'):
            continue
        source = path.read_text(errors='replace')
        rows = [(m.group(1), int(m.group(2), 16), int(m.group(3))) for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*\{[^}]*\}\s*,\s*(\d+)\s*,', source)]
        rows += [(m.group(1), int(m.group(2), 16), 6) for m in re.finditer(r'(\w*site_va)\s*=\s*(0x[0-9a-fA-F]+)', source)]
        rows += [(m.group(1), int(m.group(2), 16), 5) for m in re.finditer(r'(callsite_va)\s*=\s*(0x[0-9a-fA-F]+)', source)]
        for name, address, length in rows:
            if any(address < b and a < address + length for a, b in spans):
                conflicts.append(name)
    checks['only_the_documented_conflict'] = conflicts == [KNOWN_CONFLICT]
    header = HEADER.read_text()
    pinned = lambda name, value: re.search(r'\b%s\s*=\s*0x0*%x(?![0-9a-fA-F])' % (name, value), header) is not None
    checks['header_constants'] = (pinned('probe_site_va', PROBE_SITE) and pinned('probe_target_va', PROBE_TARGET) and pinned('lens_site_va', LENS_SITE) and pinned('lens_target_va', LENS_TARGET)
                                  and pinned('probe_gates_fnv1a', GATES[2]) and pinned('probe_gates_length', GATES[1]) and pinned('conflicting_submit_phase_va', 0x472490))
    return {'passed': all(checks.values()), 'checks': checks, 'conflicts': conflicts, 'branches_into_interior': branches_in, 'absolute_references': references,
            'probe_callers': sorted(hex(v) for v in callers[PROBE_TARGET]), 'lens_callers': sorted(hex(v) for v in callers[LENS_TARGET])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    args = parser.parse_args()
    result = verify(args.exe)
    print(json.dumps(result, indent=1))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
