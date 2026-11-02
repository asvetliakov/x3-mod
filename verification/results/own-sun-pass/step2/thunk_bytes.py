#!/usr/bin/env python3
"""Prints the raw bytes of a thunk symbol in an i686 object (objdump -d), one hex string per symbol.

  python3 thunk_bytes.py <object.o> <symbol>...

Used on 2026-11-02 to show the lens-bracket deletion left the probe thunks byte-identical: the AP module object of
verification/probe/build_own_sun_hook.py and sun_occlusion_fl.cpp built with the FL fixture's flags, before (the base
commit's tree) and after. Relocated operands print as zeros (objects, not linked images).
"""
import re
import subprocess
import sys


def thunk_bytes(obj, symbol):
    listing = subprocess.run(['i686-w64-mingw32-objdump', '-d', '-w', obj], capture_output=True, text=True, check=True).stdout
    out, inside = [], False
    for line in listing.splitlines():
        head = re.match(r'^[0-9a-f]+ <([^>]+)>:', line)
        if head:
            inside = head.group(1) == symbol
            continue
        row = re.match(r'^\s*[0-9a-f]+:\s+((?:[0-9a-f]{2} )+)', line)
        if inside and row:
            out.append(row.group(1).replace(' ', ''))
    return ''.join(out)


def thunk_listing(obj, symbol):
    """The symbol's instructions with their relocation targets (objdump -dr), addresses and raw bytes stripped: equal
    listings mean the same instruction sequence with the same operands up to link-time placement."""
    listing = subprocess.run(['i686-w64-mingw32-objdump', '-dr', '-w', '-M', 'intel', '--no-show-raw-insn', obj],
                             capture_output=True, text=True, check=True).stdout
    out, inside = [], False
    for line in listing.splitlines():
        head = re.match(r'^[0-9a-f]+ <([^>]+)>:', line)
        if head:
            inside = head.group(1) == symbol
            continue
        if inside and line.strip():
            text = re.sub(r'^\s*[0-9a-f]+:\s*', '', line)
            text = re.sub(r'\b[0-9a-f]+ <([^>+]+)(\+0x[0-9a-f]+)?>', r'<\1>', text)  # call targets: symbol only
            out.append(re.sub(r'\s+', ' ', text).strip())
    return out


if __name__ == '__main__':
    for name in sys.argv[2:]:
        print(name, thunk_bytes(sys.argv[1], name) or 'absent')
        print('  ' + ' | '.join(thunk_listing(sys.argv[1], name)))
