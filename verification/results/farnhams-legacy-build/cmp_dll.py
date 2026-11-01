#!/usr/bin/env python3
"""Byte comparison of two d3d9.dll files outside their debug sections.

docs/architecture/farnhams-legacy-build.md: the ap edition must link the same DLL as before the edition
switch. Build both from the same source path without .git (X3M_SOURCE_COMMIT "unknown" in both), then
run this: every differing byte below the first .debug* section's file offset is listed with the header
field or section it falls in. Expected: the COFF TimeDateStamp, the optional-header CheckSum, the
.debug* section headers' sizes, and the export directory's TimeDateStamp. Prints one JSON object.

usage: cmp_dll.py <baseline d3d9.dll> <candidate d3d9.dll>
"""
import json
import re
import subprocess
import sys

OBJDUMP = 'i686-w64-mingw32-objdump'


def sections(path):
    out = subprocess.run([OBJDUMP, '-h', path], capture_output=True, text=True, check=True).stdout
    # Idx Name Size VMA LMA File-off Algn
    return [(m.group(1), int(m.group(2), 16), int(m.group(3), 16))
            for m in re.finditer(r'^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)', out, re.M)]


def main():
    a, b = (open(p, 'rb').read() for p in sys.argv[1:3])
    secs = sections(sys.argv[1])
    if [(n, s) for n, s, _ in secs if not n.startswith('.debug')] != \
            [(n, s) for n, s, _ in sections(sys.argv[2]) if not n.startswith('.debug')]:
        print(json.dumps({'result': 'FAIL', 'reason': 'loadable section table differs'}))
        return 1
    first_debug = min(off for n, _, off in secs if n.startswith('.debug'))
    pe = int.from_bytes(a[0x3c:0x40], 'little')
    table = pe + 24 + int.from_bytes(a[pe + 20:pe + 22], 'little')

    def label(off):
        if pe + 8 <= off < pe + 12:
            return 'coff.TimeDateStamp'
        if pe + 24 + 64 <= off < pe + 24 + 68:
            return 'optional.CheckSum'
        if table <= off < table + 40 * len(secs):
            return 'section_header:' + secs[(off - table) // 40][0]
        for n, size, start in secs:
            if start and start <= off < start + size:  # .bss has no file bytes (offset 0)
                return n + '+0x%x' % (off - start)
        return 'other'

    diff = [i for i in range(min(len(a), len(b), first_debug)) if a[i] != b[i]]
    labels = sorted({label(i) for i in diff})
    print(json.dumps({'loadable_bytes_compared': first_debug, 'differing_bytes': len(diff), 'where': labels}, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
