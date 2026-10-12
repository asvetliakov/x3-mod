#!/usr/bin/env python3
"""Re-read every X3AP.exe byte string quoted in docs/reverse-engineering/gate-queue-script.md (story loader, save
load, opcode handlers, resource lookup order, signature flag, the natives used by the patch options) and print
RESULT PASS only if all match. Read-only; addresses are preferred VAs (image base 0x00400000).

    python3 -I verification/results/ship-navigation-ai/gate-queue/exe_checks.py [path/to/X3AP.exe] > exe_checks.txt
"""
import hashlib, struct, sys

EXE = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/X3AP.exe'
SHA = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def off(va):
    r = va - 0x400000
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs): return ra + r - sva
    raise ValueError(hex(va))


rd = lambda va, n: d[off(va):off(va) + n]
cstr = lambda va: d[off(va):d.index(b'\0', off(va))].decode('latin1')
BYTES = [
    # story loader 0x0049d030
    (0x0049d199, '83 8b f0 12 00 00 10', 'loader: VM+0x12f0 |= 0x10 (story not verified) before the check'),
    (0x0049d215, 'e8 76 dc 02 00', 'loader: call 0x004cae90 (verify the obj, NULs replaced by spaces, against addon\\x3files.xml)'),
    (0x0049d221, '83 a3 f0 12 00 00 ef', 'loader: clear bit 0x10 only when the signature verifies'),
    (0x0049d276, '3d 21 03 00 00', 'loader: version field compared with 801 (0x321)'),
    (0x0049d2a7, '89 83 3c 14 00 00', 'loader: header build id -> VM+0x143c'),
    (0x0049dc69, '8b bb 40 14 00 00', 'loader PTCH: table VM+0x1440 ...'),
    (0x0049dc71, 'e8 7a 1f 05 00', '... insert(old build id -> offset map) 0x004efbf0'),
    (0x0049e426, 'e8 c5 00 00 00', 'operand byte-swap/native resolution pass 0x0049e1a0 ends by calling the peephole pass 0x0049e4f0'),
    # interpreter
    (0x004a317a, '8b 3f 8b 54 24 18 03 7a 08', 'jmp 0x32: IP = BE32 operand (swapped to LE at load) + CODE base [VM+8]: absolute CODE offset'),
    (0x004a35bc, '0f bf 0f', 'frame 0x6e: N = s16 operand ...'),
    (0x004a35c8, '8d 4c 08 04 39 4e 10', '... capacity check: used cells + N + 4 <= task+0x10, else grow'),
    (0x004a3501, 'e8 3a 56 00 00', 'lt 0x5c: 0x004a8b40 (a < b on int/double cells; false for other types)'),
    (0x004a351e, 'e8 8d 56 00 00', 'gt 0x5d: 0x004a8bb0 (a > b; false for other types)'),
    (0x004a353b, 'e8 70 56 00 00', 'le 0x5e: !(0x004a8bb0) ...'),
    (0x004a3544, '83 c0 01', '... so "array <= int" is true (used by option B for stock compatibility)'),
    # save load 0x004a0880
    (0x004a0941, 'e8 ea c6 ff ff', 'save load: re-load the story object through 0x0049d030'),
    (0x004a0973, '3b 83 3c 14 00 00', 'save load: saved build id == object build id?'),
    (0x004a097d, '74 2a', '... equal: no remap'),
    (0x004a097f, '83 8b f0 12 00 00 08', '... differs: VM+0x12f0 |= 8 and look up a PTCH table'),
    (0x004a098f, '8b 8b 40 14 00 00', '... in VM+0x1440 ...'),
    (0x004a0997, 'e8 64 90 fc ff', '... with 0x00469a00'),
    (0x004a09a2, '75 0c', '... found: continue'),
    (0x004a09a4, 'e9 78 0a 00 00', '... not found: jmp 0x004a1421 (save load fails)'),
    # the "modified" consumers
    (0x004af539, 'f6 86 f0 12 00 00 10', 'SE_SignGameStats (SE 151): refuses when bit 0x10 is set'),
    (0x004af54e, 'e8 3d b9 01 00', 'SE_SignGameStats: re-verifies the story with 0x004cae90'),
    # resource lookup 0x004e7590 and read 0x004e9210
    (0x004e7ce1, '80 7c 24 12 00', 'lookup: loose-file match found?'),
    (0x004e7ce6, '0f 85 3d 09 00 00', '... yes: skip the catalogue scan (loose file wins over every catalogue)'),
    (0x004e7d0c, '0f bf 81 c8 00 00 00', 'catalogue scan starts at the last registered catalogue (newest first)'),
    (0x004ecab2, 'c7 80 d8 00 00 00 08 c0 57 00', 'path table 0x0057c008; +0x20 is the story path format'),
    (0x004e922b, '83 e1 03', 'read: XOR 0x33 only when the handle flags & 3 == 3 (catalogue) ...'),
    (0x004e922e, '80 f9 03 75 41', '... (loose files are read as plain bytes)'),
    (0x004e9260, '80 34 19 33', '... xor byte [ecx+ebx], 0x33'),
    (0x004a2c03, '8b 46 10 8b 17 03 c1 89 10', 'mkarr 0x28: element k <- the cell k below the top (element 0 = last pushed) ...'),
    (0x004a882e, 'c6 06 08', '... and the result cell gets tag 8 (array)'),
    (0x004ae477, '0f b6 11', 'SE_GetExprType (SE 60): returns the argument cell tag (8 = array)'),
    # natives
    (0x004b13ee, 'e8 cd cd 03 00', 'TI_WaitRandomForInterrupt (TI 6): rand(arg1) ...'),
    (0x004b13f6, '8b 4b 0b 8b 53 01', '... object = arg2, base = arg0 ...'),
    (0x004b1401, '03 d0', '... wait = arg0 + rand(arg1) ms'),
    (0x004632f5, 'e8 26 5f fb ff', 'SA_Distance (SA 272): 0x00419220 = |pos(a) - pos(b)| of two engine objects, no sector test'),
    (0x004607d4, '6a 00', 'SA_Distance: unknown object -> result 0'),
    (0x00465acc, '66 83 78 48 12', 'SA_IsWarpOutSlotAvailable (SA 325): gate (class 0x12) only ...'),
    (0x00465b00, '83 f9 06', '... 6 warp-out slots per gate'),
    (0x00443b53, '81 c1 20 4e 00 00', 'SA_StartInHangar into a gate: reserves a warp-out slot for 20000 ms'),
]
TABLES = [  # (name table, case table, index, expected name, expected case)
    (0x00579fe0, 0x004af6a4, 149, 'SE_VerifyData', 0x004af3b2), (0x00579fe0, 0x004af6a4, 151, 'SE_SignGameStats', 0x004af4fe),
    (0x0057a7f0, 0x004690f0, 272, 'SA_Distance', 0x004632c4), (0x0057a7f0, 0x004690f0, 325, 'SA_IsWarpOutSlotAvailable', 0x00465ab4),
    (0x0057a7f0, 0x004690f0, 25, 'SA_StartInHangar', 0x004626e2), (0x0057a7f0, 0x004690f0, 26, 'SA_Idle', 0x0046274e),
    (0x00579f80, None, 6, 'TI_WaitRandomForInterrupt', None), (0x00579fe0, 0x004af6a4, 60, 'SE_GetExprType', 0x004ae474),
]
OPS = {0x32: 0x004a317a, 0x33: 0x004a3188, 0x34: 0x004a31a9, 0x6e: 0x004a35ad, 0x82: 0x004a3880, 0x83: 0x004a3986,
       0x84: 0x004a3a87, 0x85: 0x004a3adb, 0x88: 0x004a3ed4}
n = fails = 0
sha = hashlib.sha256(d).hexdigest(); ok = sha == SHA; n += 1; fails += not ok
print('%s  X3AP.exe SHA-256 %s' % ('ok  ' if ok else 'FAIL', sha))
for va, hx, claim in BYTES:
    got = rd(va, len(bytes.fromhex(hx))).hex(' '); ok = got == hx; n += 1; fails += not ok
    print('%s  0x%08x %-30s %s' % ('ok  ' if ok else 'FAIL', va, got, claim))
for nt, ct, i, name, case in TABLES:
    got = cstr(struct.unpack_from('<I', d, off(nt + 4 * i))[0]); c = struct.unpack_from('<I', d, off(ct + 4 * i))[0] if ct else None
    ok = got == name and c == case; n += 1; fails += not ok
    print('%s  name table 0x%08x[%d] = %s, case %s' % ('ok  ' if ok else 'FAIL', nt, i, got, hex(c) if c else '-'))
for op, h in OPS.items():
    got = struct.unpack_from('<I', d, off(0x004a4490 + 4 * d[off(0x004a4688 + op - 1)]))[0]; ok = got == h; n += 1; fails += not ok
    print('%s  opcode 0x%02x -> handler 0x%08x (tables 0x004a4688 / 0x004a4490)' % ('ok  ' if ok else 'FAIL', op, got))
ptr = lambda va: struct.unpack_from('<I', d, off(va))[0]
ok = cstr(ptr(0x0057c008)) == 'X3AP' and cstr(ptr(0x0057c008 + 0x20)) == 'L\\%s'; n += 1; fails += not ok
print('%s  path table 0x0057c008: [+0] -> %r, [+0x20] -> %r (story file "L\\x3story" + ".obj", relative to the working '
      'directory; no "addon" prefix)' % ('ok  ' if ok else 'FAIL', cstr(ptr(0x0057c008)), cstr(ptr(0x0057c008 + 0x20))))
for va, s_ in ((0x00562424, 'obj'), (0x0054d3f8, 'x3story')):
    ok = cstr(va) == s_; n += 1; fails += not ok
    print('%s  string 0x%08x = %r' % ('ok  ' if ok else 'FAIL', va, cstr(va)))
print('RESULT %s (%d checks)' % ('PASS' if fails == 0 else 'FAIL %d' % fails, n))
