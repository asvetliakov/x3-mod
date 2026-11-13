#!/usr/bin/env python3
"""The effects DEFAULT vertex shader body 12009 draws with (FL shader/3_0/effects.fb DEFAULT, FNV-1a 64
d5e1c75351ed3f04; docs/reverse-engineering/fl/muzzle-flash-draw.md section 6) against our vs_2_0
(src/renderer/weapon_muzzle_reshade_effects_vertex_program_inc.h, src/effects/weapon_muzzle_reshade_effects_vs.hlsl).
Decodes both token streams (SM 2.0: opcode, destination and source parameter tokens, the CTAB comment) and prints
derived facts only: each program's constant table, its declared inputs and written outputs, and the instruction
multiset difference with temporary register numbers erased, so a reader sees that ours computes the game's oPos, oT0
and oD0 with the same operations and adds oT1 = v0. No program bytes or listings are written.

  python3 -I verification/results/muzzle-reshade/effects_vs_read.py [--game /mnt/c/X3FL] \
    > verification/results/muzzle-reshade/effects_vs_read_out.txt
"""
import argparse
import collections
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
OURS = ROOT / 'src/renderer/weapon_muzzle_reshade_effects_vertex_program_inc.h'
GAME_HASH = 'd5e1c75351ed3f04'
OPS = {1: 'mov', 2: 'add', 3: 'sub', 4: 'mad', 5: 'mul', 6: 'rcp', 7: 'rsq', 8: 'dp3', 9: 'dp4', 10: 'min', 11: 'max',
       31: 'dcl', 40: 'if', 42: 'else', 43: 'endif', 81: 'def'}
RTYPE = {0: 'r', 1: 'v', 2: 'c', 4: 'o', 5: 'oD', 6: 'oT', 14: 'b'}
RASTOUT = {0: 'oPos', 1: 'oFog', 2: 'oPts'}


def register(token, erase_temps):
    kind = ((token >> 28) & 7) | ((token >> 8) & 0x18)
    number = token & 0x7ff
    if kind == 4:
        return RASTOUT.get(number, 'rast%d' % number)
    if kind == 0 and erase_temps:
        return 'r'
    return '%s%d' % (RTYPE.get(kind, 't%d' % kind), number)


def destination(token, erase):
    mask = (token >> 16) & 0xf
    return register(token, erase) + ('' if mask == 0xf else '.' + ''.join('xyzw'[i] for i in range(4) if mask >> i & 1))


def source(token, erase):
    swizzle = ''.join('xyzw'[(token >> (16 + 2 * i)) & 3] for i in range(4))
    neg = '-' if ((token >> 24) & 0xf) == 1 else ''
    return neg + register(token, erase) + ('' if swizzle == 'xyzw' else '.' + swizzle)


def decode(words):
    """(ctab rows, instructions as (op, dst, srcs) with temporaries erased)."""
    ctab, instructions = [], []
    i = 1
    while i < len(words):
        token = words[i]
        op = token & 0xffff
        if op == 0xfffe:
            n = (token >> 16) & 0x7fff
            data = struct.pack('<%dI' % n, *words[i + 1:i + 1 + n])
            if data[:4] == b'CTAB':
                d = data[4:]
                count, info = struct.unpack_from('<II', d, 12)
                for k in range(count):
                    name, regset, index, regs = struct.unpack_from('<IHHH', d, info + 20 * k)
                    ctab.append((d[name:d.index(b'\0', name)].decode(), 'bcs'[regset] if regset < 3 else str(regset),
                                 index, regs))
            i += 1 + n
            continue
        if op == 0xffff:
            break
        length = (token >> 24) & 0xf
        args = words[i + 1:i + 1 + length]
        name = OPS.get(op, 'op%d' % op)
        if name == 'def':
            instructions.append((name, destination(args[0], True), tuple(f'{v:g}' for v in
                                                                      struct.unpack('<4f', struct.pack('<4I', *args[1:5])))))
        elif name == 'dcl':
            instructions.append((name, f'{args[0]:08x}', (destination(args[1], True),)))
        elif name in ('if',):
            instructions.append((name, '', tuple(source(a, True) for a in args)))
        elif name in ('else', 'endif'):
            instructions.append((name, '', ()))
        else:
            instructions.append((name, destination(args[0], True), tuple(source(a, True) for a in args[1:])))
        i += 1 + length
    return sorted(ctab), instructions


def game_program(game):
    sys.path.insert(0, str(ROOT / 'tools/analysis'))
    from effect_passes import _archive_effects, parse_container, VERTEX_SHADER_STATE
    from shader_coverage_offline import _program
    for _, path, data in _archive_effects(game, edition='fl'):
        if path != 'shader/3_0/effects.fb':
            continue
        c = parse_container(data)
        for ti, (tech, tps) in enumerate(c['techniques']):
            if tech != 'DEFAULT':
                continue
            for si, st in enumerate(tps[0][1]):
                if st[0] != VERTEX_SHADER_STATE:
                    continue
                prog = _program(data, c, ti, 0, si)
                h = prog[0] if isinstance(prog[0], str) else f'{prog[0]:016x}'
                if h == GAME_HASH:
                    _, start, _ = c['state_resources'][(ti, 0, si)]
                    return struct.unpack('<%dI' % prog[1], data[start:start + 4 * prog[1]])
    raise SystemExit(f'{GAME_HASH} not found in {game} shader/3_0/effects.fb')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game', default='/mnt/c/X3FL')
    args = parser.parse_args()
    game = game_program(args.game)
    ours = tuple(int(x, 16) for x in re.findall(r'0x([0-9a-f]{8})u', OURS.read_text()))
    for label, words in (('game', game), ('ours', ours)):
        ctab, ins = decode(words)
        outputs = sorted({d.split('.')[0] for op, d, _ in ins if d.startswith('o')})
        inputs = sorted(s[0] for op, _, s in ins if op == 'dcl')
        print(f'{label}: version {words[0]:08x}, {len(words)} words, {sum(op not in ("dcl", "def") for op, _, _ in ins)} '
              f'instructions; inputs {inputs}; outputs {outputs}')
        print('  CTAB ' + ', '.join(f'{n} {s}{i}..{s}{i + r - 1}' if r > 1 else f'{n} {s}{i}' for n, s, i, r in ctab))
    g, o = (collections.Counter(decode(w)[1]) for w in (game, ours))
    print('instructions in ours, not in the game\'s (temporaries erased):')
    for k, v in sorted((o - g).items()):
        print(f'  {v} x {k[0]} {k[1]} {", ".join(k[2])}')
    print('instructions in the game\'s, not in ours (temporaries erased):')
    for k, v in sorted((g - o).items()):
        print(f'  {v} x {k[0]} {k[1]} {", ".join(k[2])}')


if __name__ == '__main__':
    main()
