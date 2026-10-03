#!/usr/bin/env python3
"""Byte-level check of the SETA / frame-step facts in engine-effects.md section 8 (read-only, X3AP.exe).

  python3 seta_frame_step.py [X3AP.exe] > seta_frame_step_out.txt

Each row compares whole instructions (or table words / strings) at fixed VAs with the expected bytes and
prints PASS/FAIL; no game bytes beyond the compared instruction encodings are printed.
"""
import hashlib
import os
import struct
import sys

EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    '~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe')
SECTIONS = [(0x1000, 0x130630, 0x400), (0x132000, 0x4074d, 0x130c00), (0x173000, 0xefb58, 0x171400)]


def off(va):
    rva = va - 0x400000
    for v, size, raw in SECTIONS:
        if v <= rva < v + size:
            return rva - v + raw
    raise ValueError(hex(va))


SITES = [  # (va, hex bytes, meaning)
    (0x004d1eb0, '01 b1 20 07 00 00', 'tick: cfg+0x720 += wall ms (esi)'),
    (0x004d1ec3, 'a1 98 8a 60 00', 'tick: forced one-shot game step *0x00608a98'),
    (0x004d1ef0, '8b 91 d0 00 00 00', 'tick: edx = cfg+0xd0 (governor)'),
    (0x004d1ef6, '8b 81 cc 00 00 00', 'tick: eax = cfg+0xcc (time-warp factor)'),
    (0x004d1f30, '81 fe c8 00 00 00', 'tick: wall ms clamp high 200'),
    (0x004d1f5c, '01 81 18 07 00 00', 'tick: cfg+0x718 += clamp(ms,1,200)*f'),
    (0x004ee1e1, 'e8 0a 3c fe ff', 'frame clock 0x004ee1e0 calls the tick 0x004d1df0'),
    (0x004ee1f4, '2b 05 04 91 60 00', 'game step = cfg+0x718 - *0x00609104'),
    (0x004ee203, '81 f9 fe 7f 00 00', 'step - 1 > 0x7ffe -> 1'),
    (0x004ee216, '89 82 14 07 00 00', 'cfg+0x714 = game step (ms)'),
    (0x004ee21e, '2b 05 08 91 60 00', 'wall step = cfg+0x720 - *0x00609108'),
    (0x004ee23a, '89 82 1c 07 00 00', 'cfg+0x71c = wall step (ms)'),
    (0x004b0e0d, 'e8 ce d3 03 00', 'scheduler 0x004b0e00 calls the frame clock first'),
    (0x004b0fc9, 'b8 33 b3 00 00', 'governor x0.7 (0xb333) when > 1000 events due'),
    (0x004b0ff3, 'c7 81 d0 00 00 00 cc 4c 00 00', 'governor floor 0x4ccc (0.3)'),
    (0x004b1016, 'b8 33 33 01 00', 'governor x1.2 (0x13333) when < 200 events due'),
    (0x004b0f99, 'c7 81 d0 00 00 00 00 00 01 00', 'governor reset 1.0 when the warp factor drops'),
    (0x004b149c, '89 90 cc 00 00 00', 'TI_SetTimeWarpFactor: cfg+0xcc = arg'),
    (0x00403af0, 'e8 0b d3 0a 00', 'main loop calls 0x004b0e00 every iteration'),
    (0x00403af5, 'e8 b6 f9 0c 00', 'main loop pump 0x004d34b0 after the clock'),
    (0x00403ac5, 'f6 86 a0 04 00 00 01', 'main loop: pause bit test before the extra tick'),
    (0x00403f7a, 'e8 f1 fe 0d 00', 'main loop Present 0x004e3e70 (path 1)'),
    (0x00403f87, 'e8 e4 fe 0d 00', 'main loop Present 0x004e3e70 (path 2)'),
    (0x0045b01a, '69 c0 06 01 00 00', 'jet drive rate limit dt*0x106 (0.004/ms)'),
    (0x004596f7, '8b b8 18 07 00 00', 'jet drive now = cfg+0x718'),
    (0x0045b3ce, '89 83 a8 00 00 00', 'jet drive obj+0xa8 = now'),
    (0x00412113, '81 ba cc 00 00 00 00 00 01 00', 'input: warp != 1.0 -> script StopFastForward'),
    (0x00496f99, '3d 00 00 01 00', 'auto-detail: frame ms = cfg+0x714 / warp when warp > 1.0'),
    (0x00402844, 'a3 34 6f 60 00', 'config pointer published once at startup'),
    (0x004ee172, 'c7 05 34 6f 60 00 00 00 00 00', 'config pointer cleared in 0x004edf50 (exit/fatal)'),
]
TABLE = (0x00579f80, ['TI_Delay', 'TI_DelayRandom', 'TI_GetAbsTime', 'TI_CmpTime', 'TI_WaitForStart',
                      'TI_WaitForInterrupt', 'TI_WaitRandomForInterrupt', 'TI_Interrupt', 'TI_GetLastFrameDelay',
                      'TI_SetTimeWarpFactor', 'TI_GetTimeWarpFactor', 'TI_GetTimeWarpMultiplier', 'TI_AddTime',
                      'TI_SetAbsTime', 'TI_GetRealTime', 'TI_ReadSystemClock'])
JUMPS = (0x004b15f8, [0x4b1321, 0x4b1361, 0x4b143e, 0x4b1504, 0x4b13ad, 0x4b13c3, 0x4b13e2, 0x4b1417, 0x4b1465,
                      0x4b148c, 0x4b14b6, 0x4b14dd, 0x4b1524, 0x4b1545, 0x4b1577, 0x4b159d])
STRINGS = [(0x0056494c, b'%d fps, Warp: %d%% (real %d%%), %3.3f MB, %d Allocs'), (0x00555e1c, b'StopFastForward')]


def main():
    b = open(EXE, 'rb').read()
    print('exe sha256', hashlib.sha256(b).hexdigest()[:8], 'size', len(b))
    fails = 0
    for va, hexbytes, meaning in SITES:
        want = bytes.fromhex(hexbytes)
        ok = b[off(va):off(va) + len(want)] == want
        fails += not ok
        print('%s 0x%08x %-32s %s' % ('PASS' if ok else 'FAIL', va, hexbytes, meaning))
    base, names = TABLE
    for i, name in enumerate(names):
        p = struct.unpack_from('<I', b, off(base + 4 * i))[0]
        s = b[off(p):b.index(b'\0', off(p))].decode('latin1')
        handler = struct.unpack_from('<I', b, off(JUMPS[0] + 4 * i))[0]
        ok = s == name and handler == JUMPS[1][i]
        fails += not ok
        print('%s native %2d %-26s handler 0x%08x' % ('PASS' if ok else 'FAIL', i, s, handler))
    for va, want in STRINGS:
        ok = b[off(va):off(va) + len(want) + 1] == want + b'\0'
        fails += not ok
        print('%s 0x%08x string %r' % ('PASS' if ok else 'FAIL', va, want.decode()))
    print('result', 'PASS' if not fails else 'FAIL %d' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
