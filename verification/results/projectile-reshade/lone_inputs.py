#!/usr/bin/env python3
"""The lone bullet path's programs (docs/reverse-engineering/fl/bullet-draw-batch.md section 7): derived only.

A bullet body the batch path does not take is drawn through the material submit (0x0e7142 -> 0x0e0f30) with its own
material, effects.fx. Per effect of the FL effective archive namespace (root + addon2) whose name is the base alias
`effects`/`effects2s`: the technique order and, per profile directory, the DEFAULT and INSTANCE passes' (VS, PS) pairs (the proxy's
FNV-1a 64 of the program bytes); then the decoded interface of the programs the 3_0 directory carries (declarations
with their usage, CTAB register names), so the re-shade variant can read the same interpolators.

  python3 -I verification/results/projectile-reshade/lone_inputs.py [--game /mnt/c/X3FL] \
    > verification/results/projectile-reshade/lone_inputs_out.txt
No game bytes are written; nothing launches.
"""
import argparse
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from inspect_motion_output_profiles import (OPCODES, USAGES, instructions, operand, swizzle_of,  # noqa: E402
                                            mask_of, DEFINITIONS)

DCL = 31


def ctab(words):
    names, i = {}, 1
    while words[i] != 0xffff:
        if words[i] & 0xffff == 0xfffe:
            n = words[i] >> 16
            blob = struct.pack('<%dI' % n, *words[i + 1:i + 1 + n])
            if blob[:4] == b'CTAB':
                d = blob[4:]
                count, info = struct.unpack_from('<II', d, 12)
                for k in range(count):
                    name_o, regset, reg, regcount = struct.unpack_from('<IHHH', d, info + 20 * k)
                    name = d[name_o:d.index(b'\0', name_o)].decode()
                    names[(regset, reg)] = (name, regcount)
            i += 1 + n
            continue
        i += 1
    return names


def rename(name, stage):
    if stage == 'vs' and name == 'rastout0':
        return 'oPos'
    for a, b in (('attrout', 'oD'), ('o', 'oT')) if stage == 'vs' else (('a', 't'),):
        if name.startswith(a) and name[len(a):].isdigit():
            return b + name[len(a):]
    return name


def decode(words, stage):
    out = []
    _, items, _ = instructions(struct.pack('<%dI' % len(words), *words))
    for it in items:
        op = OPCODES.get(it['opcode'], 'op%d' % it['opcode'])
        w = it['words']
        if it['opcode'] == DCL:
            d = operand(w[1])
            d['name'] = rename(d['name'], stage)
            usage = USAGES.get(w[0] & 0x1f, w[0] & 0x1f)
            idx = (w[0] >> 16) & 0xf
            out.append(f'dcl_{usage}{idx if idx else ""} {d["name"]}.{mask_of(w[1])}' if stage == 'vs'
                       else f'dcl {d["name"]}.{mask_of(w[1])}' if d['name'][0] in 'vt' else f'dcl_2d {d["name"]}')
            continue
        if it['opcode'] in DEFINITIONS:
            d = operand(w[0])
            vals = struct.unpack('<4f', struct.pack('<4I', *w[1:5]))
            out.append(f'{DEFINITIONS[it["opcode"]]} {d["name"]} = ' + ', '.join(f'{v:g}' for v in vals))
            continue
        ops = []
        for k, t in enumerate(w):
            d = operand(t)
            d['name'] = rename(d['name'], stage)
            if k == 0:
                mod = ('_sat' if (t >> 20) & 1 else '')
                ops.append(d['name'] + mod + '.' + mask_of(t))
            else:
                neg = '-' if ((t >> 24) & 15) == 1 else ''
                ops.append(neg + d['name'] + '.' + swizzle_of(t))
        out.append(op + ' ' + ', '.join(ops))
    return out


def main():
    from effect_passes import _archive_effects, parse_container, VERTEX_SHADER_STATE, PIXEL_SHADER_STATE
    from shader_coverage_offline import _program
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--game', default='/mnt/c/X3FL')
    parser.add_argument('--listing', action='store_true',
                        help='also print every decoded instruction (local use only; the tracked output keeps the '
                             'declarations, the constant names and the written output lanes)')
    args = parser.parse_args()
    programs, order = {}, {}
    for _, path, data in _archive_effects(args.game, edition='fl'):
        stem = Path(path).stem
        if stem not in ('effects', 'effects2s'):
            continue
        prof = Path(path).parent.name
        c = parse_container(data)
        order.setdefault(tuple(t for t, _ in c['techniques']), []).append(f'{prof}/{stem}')
        for ti, (tech, tps) in enumerate(c['techniques']):
            if tech not in ('DEFAULT', 'INSTANCE'):
                continue
            for pi, (_, states) in enumerate(tps):
                pair = {}
                for si, st in enumerate(states):
                    if st[0] not in (VERTEX_SHADER_STATE, PIXEL_SHADER_STATE):
                        continue
                    prog = _program(data, c, ti, pi, si)
                    if not prog:
                        continue
                    stage = 'vs' if st[0] == VERTEX_SHADER_STATE else 'ps'
                    _, start, _ = c['state_resources'][(ti, pi, si)]
                    pair[stage] = prog[0]
                    programs[prog[0]] = (stage, struct.unpack('<%dI' % prog[1], data[start:start + 4 * prog[1]]))
                print(f'{path}: {tech} pass {pi} vs {pair.get("vs")} ps {pair.get("ps")}')
    for techs, where in order.items():
        print('technique order', ' '.join(f'{i}:{t}' for i, t in enumerate(techs)), 'in', len(where), 'effects')
    for h in ('d5e1c75351ed3f04', '89193868c61c3846', '8360f422de08b5bd'):
        match = [(k, v) for k, v in programs.items() if (f'{k:016x}' if isinstance(k, int) else k) == h]
        if not match:
            print(h, 'not in the archive DEFAULT passes')
            continue
        stage, words = match[0][1]
        print(f'{stage} {h} {stage}_{(words[0] >> 8) & 255}_{words[0] & 255} dwords {len(words)}')
        for (regset, reg), (name, n) in sorted(ctab(words).items()):
            kind = 's' if regset == 3 else 'b' if regset == 0 else 'c'
            print(f'  ctab {kind}{reg}..{kind}{reg + n - 1} {name}')
        lines = decode(words, stage)
        writes, reads = {}, set()
        for line in lines:
            if line.startswith('dcl'):
                print('  ' + line)
                continue
            parts = line.split(' ', 1)
            if len(parts) < 2 or line.startswith('def'):
                continue
            ops = [o.strip() for o in parts[1].split(',')]
            dest = ops[0].split('.')
            if dest[0][:2] in ('oP', 'oT', 'oD', 'oC'):
                writes.setdefault(dest[0], set()).update(dest[1] if len(dest) > 1 else 'xyzw')
            for o in ops[1:]:
                r = o.lstrip('-').split('.')[0]
                if r[0] in 'vtcs' and r[1:].isdigit():
                    reads.add(r)
        print('  writes ' + ' '.join(f'{r}.{"".join(sorted(m, key="xyzw".index))}' for r, m in sorted(writes.items())))
        print('  reads ' + ' '.join(sorted(reads, key=lambda r: (r[0], int(r[1:])))))
        if args.listing:
            for line in lines:
                print('  ' + line)


if __name__ == '__main__':
    main()
