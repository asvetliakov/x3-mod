#!/usr/bin/env python3
"""Byte witnesses for docs/reverse-engineering/sector-transit-order.md (read-only, installed X3AP.exe).

Checks the EXE identity and the instruction bytes of every site the note cites for the object list,
the sector fields and the two candidate write sites. Prints ok/MISMATCH per site; exit 1 on any mismatch.
"""
import hashlib, os, struct, sys

EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    "~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe")
SHA = "fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab"
SITES = [
    # (VA, expected bytes, meaning)
    (0x0043f93b, "a1 0c 85 60 00 8b 48 10 89 4e 04 8b 50 10 89 32 8b 15 34 6f 60 00 8d 48 0c 89 0e 89 70 10",
     "object ctor 0x0043f900: AddTail to the global list at [0x0060850c]+8 (lh_TailPred at +0x10, node prev at +4)"),
    (0x00442c31, "a1 0c 85 60 00 8b 48 10 89 4f 04 8b 50 10 89 3a 8d 48 0c 89",
     "save-load post-pass 0x00442c00: parentless object (sector) AddTail to the same list"),
    (0x004495e5, "89 53 04 8b 48 08 89 19 8d 50 04 89 13 89 58 08 0f b7 43 48 66 3d 03 00 89 73 54",
     "SA_StartObjectInSpace 0x00449510: link into sector child list, [obj+0x54]=sector before scene attach"),
    (0x0043fc0b, "8b 4d 04 8b 55 00 89 11 8b 45 00 8b 4d 04 68 30 01 00 00 53 89 48 04 55 66 c7 85 9c 00 00 00 ac ef",
     "object free tail 0x0043f990: Remove from list, +0x9c = 0xefac (alive marker 0xcafe)"),
    (0x004524f0, "89 86 48 01 00 00 89 86 3c 01 00 00",
     "sector ctor 0x004524d0: +0x148 flags, +0x13c index = 0"),
    (0x004644a0, "e8 bb 60 fd ff 85 c0 0f 84 a6 c2 ff ff 8b 4e 06 89 88 3c 01 00 00 51 e9",
     "SA_SetSectorBackgroundType (0x00460630 case 0x133): [sector+0x13c] = arg"),
    (0x0042d66e, "8b ce 89 46 54 e8 e8 2c ff ff",
     "INS_CockpitSetSectorSpace (0x0042d340 case 0xb): [cockpit+0x54] = sector; call 0x00420360"),
    (0x0043f8be, "89 43 38",
     "universe restore 0x0043f420: [M+0x38] = player ship"),
]
data = open(EXE, "rb").read()
ok = hashlib.sha256(data).hexdigest() == SHA
print("exe sha256", "ok" if ok else "MISMATCH")
pe = struct.unpack_from("<I", data, 0x3c)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]; opt = struct.unpack_from("<H", data, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    vs, va, rs, ra = struct.unpack_from("<IIII", data, o + 8); secs.append((0x400000 + va, rs, ra))
def off(v):
    for va, rs, ra in secs:
        if va <= v < va + rs: return ra + v - va
    raise ValueError(hex(v))
for va, hexbytes, what in SITES:
    exp = bytes.fromhex(hexbytes.replace(" ", ""))
    got = data[off(va):off(va) + len(exp)]
    good = got == exp; ok &= good
    print("%08x %-8s %s" % (va, "ok" if good else "MISMATCH", what))
sys.exit(0 if ok else 1)
