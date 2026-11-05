#!/usr/bin/env python3
"""Instruction checks behind the shield read recipe (weapon-effect-spawn.md section 9; read-only).

  python3 shield_layout_sites.py <objdump listing>

Prints each instruction the recipe rests on and checks its text against the expected operand, so a different EXE
or a misread fails loudly. Exit status 1 on any mismatch.
"""
import re
import sys

CHECKS = [  # address, expected substring, what it shows
    (0x00449ab9, 'mov eax,DWORD PTR [ecx+0x50]', 'damage stage: victim extension'),
    (0x00449abc, 'mov edi,DWORD PTR [eax+0x6c]', 'damage stage: first node of the shield list (ext+0x6c)'),
    (0x00449ac7, 'cmp DWORD PTR [edi],esi', 'damage stage: the node whose +0 is 0 ends the list'),
    (0x00449acf, 'mov ecx,DWORD PTR [edi+0x50]', 'damage stage: shield object extension'),
    (0x00449ad2, 'mov ecx,DWORD PTR [ecx+0x4]', 'damage stage: shield energy (ext+4)'),
    (0x00449ad9, 'movsx eax,WORD PTR [edi+0x4a]', 'damage stage: TShields index = shield subtype'),
    (0x00449add, 'mov edx,DWORD PTR ds:0x606fdc', 'TShields table pointer (class 9)'),
    (0x00449ae9, 'mov eax,DWORD PTR [eax+edx*1+0x48]', 'TShields +0x48 capacity (col 8)'),
    (0x00449b01, 'mov edx,0x3e80000', 'damage -> energy: d*0x3e80000/capacity'),
    (0x00449b4a, 'mov DWORD PTR [ecx+0x4],esi', 'drained shield set to 0 (ESI = 0)'),
    (0x00449b8a, 'sub DWORD PTR [edi+0x4],eax', 'absorbing shield loses the converted damage'),
    (0x00449a74, 'xor ebx,ebx', 'EBX = shield points removed by this hit, starts at 0'),
    (0x00449b51, 'add ebx,eax', 'EBX += points of a drained shield'),
    (0x00449b8d, 'add ebx,ebp', 'EBX += remainder taken by the absorbing shield'),
    (0x00449bce, 'cmp DWORD PTR [esp+0x18],esi', 'absorbed flag test: skip the hull stage'),
    (0x00449cce, 'add ebx,edi', 'EBX += hull damage (hull stage only)'),
    (0x00449bfd, 'mov DWORD PTR [esp+0x28],eax', 'shield-damage argument slot rewritten (flag 0xa0 path only)'),
    (0x004402a2, 'mov DWORD PTR [eax+0x4],0x3e80000', 'new shield object starts full (class-9 case 0x0044024b)'),
    (0x0044fa55, 'cmp edx,0x3e80000', 'recharge clamps at full'),
    (0x0044fa5d, 'mov DWORD PTR [ecx+0x4],0x3e80000', 'recharge clamp store'),
    (0x004407f9, 'mov DWORD PTR [ebp+0x40],0x100000', 'class 6 (factory) creation: shielded flag bit 20'),
    (0x004409bc, 'mov DWORD PTR [ebp+0x40],0x100000', 'class 5 (dock) creation: shielded flag bit 20'),
    (0x00440621, 'or DWORD PTR [ebp+0x40],0x100000', 'class 7 (ship) creation: shielded flag bit 20'),
    (0x00414d98, 'test eax,0x80000', 'effect element EEDF_HULLDAMAGE gate'),
    (0x00414d9f, 'mov edx,DWORD PTR [ebx+0x10]', 'gate object = instance obj (the hit object)'),
    (0x00414da2, 'call 0x44ecd0', 'current shield sum'),
    (0x00414da7, 'cmp eax,0x64', 'element runs only below 100'),
]


def main():
    ins = {}
    for line in open(sys.argv[1]):
        m = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)', line)
        if m and m.group(2).strip():
            ins[int(m.group(1), 16)] = re.sub(r'\s+', ' ', m.group(2).strip())
    bad = 0
    for addr, want, what in CHECKS:
        got = ins.get(addr, '<no instruction>')
        ok = want in got
        bad += not ok
        print(f'{"ok  " if ok else "FAIL"} 0x{addr:08x} {got:45s} {what}')
    print(f'{len(CHECKS) - bad}/{len(CHECKS)} checks pass')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
