#!/usr/bin/env python3
"""Phase 0 site checks for the two call redirects in 0x00414590 (read-only; prints addresses, bytes and counts).

Owning note: docs/reverse-engineering/engine-effects.md section 7.

  python3 phase0_sites.py [X3AP.exe] [objdump listing]

The EXE defaults to the bottle X3 install (only read). The optional listing is a local, untracked
`i686-w64-mingw32-objdump -d -M intel --no-show-raw-insn X3AP.exe` output; with it the script also scans every
direct branch for targets inside the two five-byte windows.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

EXE = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'
SITES = {  # name: (call address, expected callee, verification window start, window end (exclusive))
    'A effect instance 0x004148a0': (0x004147eb, 0x004148a0, 0x004147c4, 0x004147f3),
    'B trail generator 0x00412d70': (0x0041482c, 0x00412d70, 0x004147ff, 0x00414831),
}


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    out = []
    for i in range(count):
        o = pe + 24 + opt + 40 * i
        name = data[o:o + 8].rstrip(b'\0').decode()
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
        out.append((name, base + va, min(vsize, rsize), raw))
    return base, out


def main():
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else EXE
    data = exe.read_bytes()
    base, secs = sections(data)
    print(f'{exe.name} sha256 {hashlib.sha256(data).hexdigest()[:16]} base {base:#010x}')

    def rd(va, n):
        for _, start, size, raw in secs:
            if start <= va < start + size:
                return data[raw + va - start: raw + va - start + n]
        raise ValueError(hex(va))

    text = next(s for s in secs if s[0] == '.text')
    tbytes = data[text[3]:text[3] + text[2]]

    def e8_callers(target):
        out = []
        for m in re.finditer(rb'\xe8', tbytes):
            off = m.start()
            if off + 5 <= len(tbytes):
                va = text[1] + off
                if va + 5 + struct.unpack_from('<i', tbytes, off + 1)[0] == target:
                    out.append(va)
        return out

    print(f'E8 callers of 0x00414590: {[hex(x) for x in e8_callers(0x00414590)]}')
    for name, (site, callee, w0, w1) in SITES.items():
        b = rd(site, 5)
        target = site + 5 + struct.unpack_from('<i', b, 1)[0]
        q0 = site & ~7
        atomic = site + 5 <= q0 + 8
        callers = e8_callers(callee)
        print(f'{name}: site {site:#010x} bytes {b.hex(" ")} -> {target:#010x} ok={b[0] == 0xe8 and target == callee}')
        print(f'  rel32 at {site + 1:#010x}..{site + 4:#010x}; aligned qword {q0:#010x}..{q0 + 7:#010x} '
              f'holds all five bytes: {atomic}; page {site & ~0xfff:#010x} '
              f'(window crosses page: {(site & ~0xfff) != ((site + 4) & ~0xfff)})')
        print(f'  verification window {w0:#010x}..{w1 - 1:#010x} ({w1 - w0} bytes): {rd(w0, w1 - w0).hex()}')
        print(f'  all E8 callers of {callee:#010x} ({len(callers)}): {" ".join(hex(x) for x in callers)}')
        inside = set(range(site + 1, site + 5))
        ptr_hits = []
        for sname, start, size, raw in secs:
            if sname == '.rsrc':
                continue
            blob = data[raw:raw + size]
            for a in inside | {site}:
                for m in re.finditer(re.escape(struct.pack('<I', a)), blob):
                    ptr_hits.append((sname, hex(start + m.start()), hex(a)))
        print(f'  image dwords equal to an address in [{site:#x}, {site + 4:#x}]: {ptr_hits or "none"}')
    if len(sys.argv) > 2:
        branch = re.compile(r'^\s+([0-9a-f]+):\s+(j\w+|call|loop\w*)\s+0x([0-9a-f]+)\s*$')
        hits = {name: [] for name in SITES}
        n = 0
        with open(sys.argv[2]) as f:
            for line in f:
                m = branch.match(line)
                if not m:
                    continue
                n += 1
                t = int(m.group(3), 16)
                for name, (site, *_rest) in SITES.items():
                    if site < t < site + 5:
                        hits[name].append(m.group(1))
        print(f'direct branches scanned: {n}; targets strictly inside a window: {hits}')


if __name__ == '__main__':
    main()
