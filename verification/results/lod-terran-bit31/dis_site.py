#!/usr/bin/env python3
"""Print VA, raw bytes and capstone text for X3AP.exe ranges (read-only; derived text only).
Usage: dis_site.py START END [START END ...]  (hex VAs)."""
import struct, sys, os, hashlib
import capstone
EXE = os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe')
data = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    name = data[o:o + 8].rstrip(b'\0').decode()
    vs, va, rs, ro = struct.unpack_from('<IIII', data, o + 8)
    secs.append((name, base + va, vs, ro, rs))
def off(v):
    for n, va, vs, ro, rs in secs:
        if va <= v < va + max(vs, rs):
            return ro + (v - va)
    raise ValueError(hex(v))
if __name__ == '__main__':
    print('sha256', hashlib.sha256(data).hexdigest())
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    a = sys.argv[1:]
    for i in range(0, len(a), 2):
        s, e = int(a[i], 16), int(a[i + 1], 16)
        print('=== %08x..%08x' % (s, e))
        for ins in md.disasm(data[off(s):off(e)], s):
            print('%08x  %-24s %s %s' % (ins.address, ins.bytes.hex(' '), ins.mnemonic, ins.op_str))
