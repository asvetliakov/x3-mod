#!/usr/bin/env python3
"""Map a 32-bit VA to the PE image whose preferred [ImageBase, ImageBase+SizeOfImage) covers it.
Scans Wine's i386-windows builtins, the bottle's syswow64/system32 and C:\\X3 DLLs. Reads PE headers only."""
import os, struct, sys
ADDRS = [int(a, 16) for a in (sys.argv[1:] or ["76B137FB", "03B88794"])]
APP = "/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/wine/i386-windows"
BOT = os.path.expanduser("~/Library/Application Support/CrossOver/Bottles/X3/drive_c")
ROOTS = [APP, f"{BOT}/windows/syswow64", f"{BOT}/X3"]
def pe(path):
    try:
        with open(path, "rb") as f: b = f.read(4096)
        if b[:2] != b"MZ": return None
        o = struct.unpack_from("<I", b, 0x3c)[0]
        if b[o:o+4] != b"PE\0\0" or struct.unpack_from("<H", b, o+24)[0] != 0x10b: return None
        opt = o + 24
        ib, soi = struct.unpack_from("<I", b, opt+28)[0], struct.unpack_from("<I", b, opt+56)[0]
        ep = struct.unpack_from("<I", b, opt+16)[0]
        return ib, soi, ep
    except OSError: return None
hits = {a: [] for a in ADDRS}
count = 0
for root in ROOTS:
    if not os.path.isdir(root): print(f"missing {root}"); continue
    for n in sorted(os.listdir(root)):
        if not n.lower().endswith((".dll", ".exe", ".drv", ".ocx", ".acm", ".ax")): continue
        r = pe(os.path.join(root, n))
        if not r: continue
        count += 1
        ib, soi, ep = r
        for a in ADDRS:
            if ib <= a < ib + soi: hits[a].append((root, n, ib, soi, a - ib))
print(f"scanned_pe32={count}")
for a, h in hits.items():
    print(f"{a:#010x}: {len(h)} covering image(s)")
    for root, n, ib, soi, rva in h: print(f"   {n} base={ib:#x} size={soi:#x} rva={rva:#x} root={os.path.basename(root)}")
