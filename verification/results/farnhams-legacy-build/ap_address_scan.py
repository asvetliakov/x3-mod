#!/usr/bin/env python3
"""X3AP.exe absolute addresses in a built d3d9.dll, and whether any is reachable.

docs/architecture/farnhams-legacy-build.md: the FL edition must carry no 0x400000-based
X3AP.exe address in reachable code. This scan disassembles the DLL's .text, records every
function holding a constant in the X3AP image range [0x401000, 0x6f5000) (immediate or
displacement), and computes a conservative reachability set: roots are the PE entry point, the
exports, and every function whose start address appears as a 32-bit word (any alignment) in
.data, .rdata or .tls (vtables, callback tables, the constructor lists, stored pointers); edges
are direct call/jmp targets plus address operands (a hook stub whose address an installer writes
is reachable only when that installer is). Indirect calls are covered through the data roots and
the address-operand edges, so the set over-approximates what can run. Prints one JSON object.

usage: ap_address_scan.py <d3d9.dll> [--list N] [--why]
"""
import argparse
import bisect
import json
import re
import struct
import subprocess
from collections import defaultdict

OBJDUMP = 'i686-w64-mingw32-objdump'
# Pointer-holding sections: .data/.rdata (vtables, callback tables, __CTOR_LIST__/__DTOR_LIST__) and
# .tls. Not .eh_frame (every FDE names its function, which would make everything a root) or .text.
DATA_SECTIONS = ('.data', '.rdata', '.tls')
AP_LOW, AP_HIGH = 0x00401000, 0x006f5000  # X3AP.exe .text start .. base + SizeOfImage 0x2f5000


def sections(path):
    out = subprocess.run([OBJDUMP, '-h', path], capture_output=True, text=True, check=True).stdout
    secs = []
    for m in re.finditer(r'^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)', out, re.M):
        # objdump -h columns: Idx Name Size VMA LMA File-off Algn
        secs.append((m.group(1), int(m.group(3), 16), int(m.group(2), 16), int(m.group(4), 16)))
    return secs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dll')
    ap.add_argument('--list', type=int, default=40)
    ap.add_argument('--why', action='store_true', help='add the root-to-function path of each listed function')
    args = ap.parse_args()
    data = open(args.dll, 'rb').read()
    secs = sections(args.dll)
    head = subprocess.run([OBJDUMP, '-p', args.dll], capture_output=True, text=True, check=True).stdout
    image_base = int(re.search(r'^ImageBase\s+([0-9a-f]+)', head, re.M).group(1), 16)
    entry = int(re.search(r'^AddressOfEntryPoint\s+([0-9a-f]+)', head, re.M).group(1), 16) + image_base
    exports = {int(m.group(1), 16) + image_base
               for m in re.finditer(r'^\s*\[\s*\d+\]\s+\+base\[\s*\d+\]\s+([0-9a-f]+)', head, re.M)}

    func_start = {}
    starts = []
    consts = defaultdict(set)   # function -> AP constants
    edges = defaultdict(set)    # function -> direct call/jmp target functions
    taken = set()               # functions whose address is an operand
    current = None
    proc = subprocess.Popen([OBJDUMP, '-d', '-C', '--no-show-raw-insn', '-j', '.text', args.dll],
                            stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    head_re = re.compile(r'^([0-9a-f]{8}) <(.*)>:$')
    insn_re = re.compile(r'^\s*([0-9a-f]+):\s+(.*)$')
    target_re = re.compile(r'\b(?:call|jmp|j[a-z]+)\s+([0-9a-f]+) <')
    hex_re = re.compile(r'0x([0-9a-f]+)')
    pending_edges = []
    pending_taken = []
    for line in proc.stdout:
        m = head_re.match(line)
        if m:
            current = m.group(2)
            addr = int(m.group(1), 16)
            func_start[addr] = current
            continue
        m = insn_re.match(line)
        if not m or current is None:
            continue
        text = m.group(2)
        t = target_re.search(text)
        if t:
            pending_edges.append((current, int(t.group(1), 16)))
        for hm in hex_re.finditer(text):
            v = int(hm.group(1), 16)
            # A displacement on a base register (0x5dd8cc(%ebx)) is a field offset, not an address.
            based = text.startswith('(%', hm.end())
            if AP_LOW <= v < AP_HIGH and not based:
                consts[current].add(v)
            elif v >= image_base:
                pending_taken.append((current, v))
    proc.wait()
    starts = sorted(func_start)

    def owner(addr):
        i = bisect.bisect_right(starts, addr) - 1
        return func_start[starts[i]] if i >= 0 else None

    for src, tgt in pending_edges:
        o = func_start.get(tgt) or owner(tgt)
        if o and o != src:
            edges[src].add(o)
    for src, v in pending_taken:
        if v in func_start and func_start[v] != src:
            taken.add(func_start[v])
            edges[src].add(func_start[v])  # an address operand counts only where the taking code is reachable
    roots = set()
    for a in exports | {entry}:
        o = func_start.get(a) or owner(a)
        if o:
            roots.add(o)
    start_set = set(func_start)
    data_roots = set()
    for name, vma, size, off in secs:
        if name not in DATA_SECTIONS:
            continue
        blob = data[off:off + size]
        for i in range(0, len(blob) - 3):
            w = struct.unpack_from('<I', blob, i)[0]
            if w in start_set:
                data_roots.add(func_start[w])
    roots |= data_roots
    reach = {}
    stack = [(r, None) for r in roots]
    while stack:
        f, parent = stack.pop()
        if f in reach:
            continue
        reach[f] = parent
        stack.extend((g, f) for g in edges.get(f, ()))

    def path(f):
        out = []
        while f is not None and len(out) < 12:
            out.append(f[:90])
            f = reach[f]
        kind = 'data' if out and out[-1] in {n[:90] for n in data_roots} else 'operand/export'
        return list(reversed(out)) + ['<- root:' + kind]
    with_consts = sorted(consts)
    reachable = sorted(f for f in with_consts if f in reach)
    print(json.dumps({
        'dll': args.dll, 'image_base': hex(image_base), 'functions': len(func_start), 'roots': len(roots),
        'address_taken': len(taken), 'roots_data': len(data_roots),
        'reachable_functions': len(reach), 'functions_with_ap_constants': len(with_consts),
        'reachable_with_ap_constants': len(reachable),
        'reachable_list': [[f[:160], sorted(hex(v) for v in consts[f])[:4]] + ([path(f)] if args.why else [])
                           for f in reachable[:args.list]],
    }, indent=1))


if __name__ == '__main__':
    main()
