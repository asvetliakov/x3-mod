#!/usr/bin/env python3
"""Call-site checks for the weapon/impact/explosion effect hook candidates (read-only; addresses, bytes, counts).

Owning note: docs/reverse-engineering/weapon-effect-spawn.md.

  python3 weapon_effect_sites.py <X3AP.exe> <objdump listing>

The listing is a local, untracked `i686-w64-mingw32-objdump -d -M intel X3AP.exe` output. For every site the
script prints the five call bytes and the decoded callee, whether the five bytes / the rel32 stay inside one
aligned qword (atomic `lock cmpxchg8b` claim) and whether the site is the instruction after a direct branch
target; then it scans every direct branch and every image dword for targets strictly inside the five bytes, and
lists every E8 caller of each callee.
"""
import hashlib
import re
import struct
import sys

SITES = {  # name: (call address, expected callee)
    'impact   0x00449d85 -> 0x00413c40': (0x00449d85, 0x00413c40),
    'muzzle   0x00446268 -> 0x00413c00': (0x00446268, 0x00413c00),
    'death A  0x0044ac64 -> 0x00413610': (0x0044ac64, 0x00413610),
    'death B  0x0044acb2 -> 0x00413610': (0x0044acb2, 0x00413610),
    'breakup  0x0041575c -> 0x004132d0': (0x0041575c, 0x004132d0),
    'flight   0x004146c0 -> 0x004148a0': (0x004146c0, 0x004148a0),
    'trail    0x004146fa -> 0x00412d70': (0x004146fa, 0x00412d70),
    'object   0x0045ac7d -> 0x00414590': (0x0045ac7d, 0x00414590),
    'hit inst 0x00413da8 -> 0x00414a70': (0x00413da8, 0x00414a70),
    'hit inst 0x00413e79 -> 0x00414a70': (0x00413e79, 0x00414a70),
}
SECS = []


def off(data, va):
    for name, start, size, raw in SECS:
        if start <= va < start + size:
            return va - start + raw
    raise ValueError(hex(va))


def main():
    exe, listing = sys.argv[1], sys.argv[2]
    data = open(exe, 'rb').read()
    print('X3AP.exe sha256', hashlib.sha256(data).hexdigest())
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    for i in range(count):
        o = pe + 24 + opt + 40 * i
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
        SECS.append((data[o:o + 8].rstrip(b'\0').decode(), base + va, min(vsize, rsize), raw))
    branches, calls = [], {}
    for line in open(listing):
        m = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(j\w+|call)\s+0x([0-9a-f]+)\s*$', line)
        if m:
            src, op, dst = int(m.group(1), 16), m.group(2), int(m.group(3), 16)
            branches.append((src, dst))
            if op == 'call':
                calls.setdefault(dst, []).append(src)
    targets = {dst for _, dst in branches}
    dwords = {}
    for name, start, size, raw in SECS:
        for k in range(raw, raw + size - 3):
            dwords.setdefault(struct.unpack_from('<I', data, k)[0], []).append(k)
    for name, (site, callee) in SITES.items():
        b = data[off(data, site):off(data, site) + 5]
        dst = (site + 5 + struct.unpack_from('<i', b, 1)[0]) & 0xffffffff
        inside = [hex(s) for s, d in branches if site < d < site + 5]
        ptrs = [v for v in range(site + 1, site + 5) if v in dwords]
        atomic5 = (site & 7) + 5 <= 8
        print(f'{name}: bytes {b.hex(" ")} -> 0x{dst:08x} ({"ok" if b[0] == 0xe8 and dst == callee else "MISMATCH"}); '
              f'five bytes in one qword: {atomic5}; branch target at site: {site in targets}; '
              f'branches into bytes 1..4: {inside or "none"}; image dwords into bytes 1..4: {len(ptrs)}; '
              f'E8 callers of 0x{callee:08x}: {len(calls.get(callee, []))} '
              f'[{" ".join(hex(c) for c in calls.get(callee, []))}]')
    print(f'direct branches scanned: {len(branches)}')


if __name__ == '__main__':
    main()
