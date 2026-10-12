#!/usr/bin/env python3
"""Re-check the X3AP.exe instruction sites behind the unit scale (owning note section 1); prints PASS/FAIL per site.

Needs capstone. Reads /mnt/c/X3/X3AP.exe read-only; prints decoded instructions only.
"""
import hashlib, struct, sys
from pathlib import Path
import capstone
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import PE32

EXE = Path('/mnt/c/X3/X3AP.exe')
b = EXE.read_bytes(); pe = PE32(b)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def off(va):
    r = va - pe.base
    for s in pe.sections:
        if s['rva'] <= r < s['rva'] + s['raw_size']: return r - s['rva'] + s['offset']


def ins(va, n=1):
    return [f'{i.mnemonic} {i.op_str}' for i in list(md.disasm(b[off(va):off(va) + 16 * n], va))[:n]]


def f32(va): return struct.unpack_from('<f', b, off(va))[0]


def cstr(va):
    o = off(va); return b[o:b.index(b'\0', o)].decode()


CHECKS = [
    # Globals name table: id -> name (pairs name*, id at 0x00552b50 + 8*(id-1))
    ('global 0x60 name', lambda: cstr(struct.unpack_from('<I', b, off(0x552b50 + 8 * (0x60 - 1)))[0]), 'SG_MAX_DISTANCE_COMM'),
    ('global 0x7a name', lambda: cstr(struct.unpack_from('<I', b, off(0x552b50 + 8 * (0x7a - 1)))[0]), 'SG_TRACTOR_BREAK_DIST'),
    ('global 0x7d name', lambda: cstr(struct.unpack_from('<I', b, off(0x552b50 + 8 * (0x7d - 1)))[0]), 'SG_TRACTOR_SPEED_LIMIT'),
    # metres -> engine units at load (0x004333a0)
    ('COMM x500', lambda: ins(0x4333f8, 2), None),
    ('TRACTOR_BREAK x500', lambda: ins(0x433daa, 2), None),
    ('TRACTOR_SPEED x500', lambda: ins(0x433df9, 2), None),
    # tractor: speed limit vs object speed [obj+0x10]; break distance vs position distance
    ('speed limit vs obj+0x10', lambda: ins(0x44a54a, 1), None),
    ('break dist vs |dpos|', lambda: ins(0x44a72d, 1), None),
    # angles: 65536 per turn; rate * dt_ms / 1000
    ('angle scale 1/65536', lambda: f32(0x5654e0), 1 / 65536),
    ('angle scale 2pi', lambda: abs(f32(0x5654f8) - 6.283185307) < 1e-6, True),
    ('mask 0xffff in 0x004f0270', lambda: ins(0x4f0273, 1), ['mov eax, 0xffff']),
    ('rate*dt/1000', lambda: ins(0x459f0f, 5), None),
    ('dt clamp 1000 ms', lambda: ins(0x4597e5, 1), ['cmp esi, 0x3e8']),
    # TShips getters: rotation x (1 + rudder tuning), accel x (1 + engine tuning)
    ('rot X getter +8', lambda: ins(0x450d92, 1), ['mov ecx, dword ptr [edx + eax + 8]']),
    ('accel getter +0x48', lambda: ins(0x450bd9, 1), ['mov eax, dword ptr [edx + eax + 0x48]']),
    # laser range = TBullets(+0x50 * +0x54) / 1000
    ('laser range getter', lambda: ins(0x433eae, 1) + ins(0x433ec7, 3), None),
    # collision radius: 0x00488170 caches max into +0xa4; type-table create calls it
    ('radius from 0x488170', lambda: ins(0x441c38, 2), ['call 0x488170', 'mov dword ptr [ebp + 0xa4], eax']),
]


def main():
    print('X3AP.exe sha256', hashlib.sha256(b).hexdigest())
    fails = 0
    for label, fn, want in CHECKS:
        got = fn()
        ok = want is None or (abs(got - want) < 1e-12 if isinstance(want, float) and not isinstance(want, bool) else got == want)
        fails += not ok
        print(f"{'PASS' if ok else 'FAIL'} {label}: {got}")
    print('fails', fails)
    return fails


if __name__ == '__main__':
    sys.exit(main())
