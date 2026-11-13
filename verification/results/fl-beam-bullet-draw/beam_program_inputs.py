#!/usr/bin/env python3
"""Inputs of the classic bullet draw programs (docs/reverse-engineering/fl/beam-bullet-draw.md section 4): derived only.

For the FL effective archive (root + addon2), the standard_lighting / standard_lighting2s DEFAULT pass of the 3_0
directory: the (VS, PS) pair hashes (the proxy's FNV-1a 64 of the program bytes), and per program its declarations,
CTAB register names, the output lanes written and, per output, the CTAB parameters and inputs it depends on (a
backward slice over the straight-line program; control flow marks the slice incomplete). Then the effects INSTANCE_BULLETS
pair of the 3_0 `effects` file, for comparison.

  python3 -I verification/results/fl-beam-bullet-draw/beam_program_inputs.py [--game /mnt/c/X3FL] [--listing] \
    > verification/results/fl-beam-bullet-draw/beam_program_inputs_out.txt
--listing prints the decoded instructions (local use only, not tracked). No game bytes are written; nothing launches.
"""
import argparse
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/projectile-reshade'))
from lone_inputs import ctab, decode  # noqa: E402

FLOW = ('if', 'else', 'endif', 'loop', 'endloop', 'rep', 'endrep', 'call', 'callnz', 'ret', 'label', 'break',
        'breakc', 'ifc', 'texkill')


def slices(lines, names):
    """Backward slice per output register: the CTAB names (or raw c#/b#) and inputs (v#/t#/s#) it depends on."""
    body = [l for l in lines if not l.startswith(('dcl', 'def'))]
    complete = not any(l.split(' ', 1)[0].split('_')[0] in FLOW for l in body)
    deps = {}   # temp/output register -> set of leaves

    def leaves(op):
        r = op.lstrip('-').split('.')[0].split('[')[0]
        if r == 'aL':
            return {'aL'}
        if r and r[0] == 'r':
            return set(deps.get(r, {'undef:' + r}))
        if r and r[0] in 'cbi' and r[1:].isdigit():
            kind = {'c': 2, 'b': 0, 'i': 1}[r[0]]
            for (rs, reg), (nm, n) in names.items():
                if rs == kind and reg <= int(r[1:]) < reg + n:
                    return {nm}
            return {r + '(def)'}
        return {r}
    for l in body:
        parts = l.split(' ', 1)
        if len(parts) < 2:
            continue
        ops = [o.strip() for o in parts[1].split(',')]
        dest = ops[0].split('.')[0].replace('_sat', '')
        if not dest:
            continue
        src = set()
        for o in ops[1:]:
            src |= leaves(o)
        if '.' in ops[0] and len(ops[0].split('.')[1]) < 4:
            src |= deps.get(dest, set())   # partial write keeps other lanes' dependencies
        deps[dest] = src
    return {k: sorted(v) for k, v in deps.items() if k[:2] in ('oP', 'oT', 'oD', 'oC')}, complete


def main():
    from effect_passes import _archive_effects, parse_container, VERTEX_SHADER_STATE, PIXEL_SHADER_STATE
    from shader_coverage_offline import _program
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--game', default='/mnt/c/X3FL')
    ap.add_argument('--listing', action='store_true')
    a = ap.parse_args()
    wanted = {('standard_lighting', 'DEFAULT'), ('standard_lighting2s', 'DEFAULT'), ('effects', 'INSTANCE_BULLETS')}
    programs = {}
    for _, path, data in _archive_effects(a.game, edition='fl'):
        p = Path(path)
        if p.parent.as_posix() != 'shader/3_0':
            continue
        c = parse_container(data)
        for ti, (tech, tps) in enumerate(c['techniques']):
            if (p.stem, tech) not in wanted:
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
                    h = prog[0] if isinstance(prog[0], str) else f'{prog[0]:016x}'
                    pair[stage] = h
                    programs[h] = (stage, struct.unpack('<%dI' % prog[1], data[start:start + 4 * prog[1]]))
                print(f'{path}: {tech} pass {pi} vs {pair.get("vs")} ps {pair.get("ps")}')
    for h, (stage, words) in sorted(programs.items(), key=lambda kv: kv[1][0], reverse=True):
        names = ctab(words)
        lines = decode(words, stage)
        print(f'{stage} {h} {stage}_{(words[0] >> 8) & 255}_{words[0] & 255} dwords {len(words)}')
        for (rs, reg), (nm, n) in sorted(names.items()):
            kind = 's' if rs == 3 else 'b' if rs == 0 else 'i' if rs == 1 else 'c'
            print(f'  ctab {kind}{reg}..{kind}{reg + n - 1} {nm}')
        for l in lines:
            if l.startswith('dcl'):
                print('  ' + l)
        sl, complete = slices(lines, names)
        print(f'  slice {"complete" if complete else "INCOMPLETE (control flow)"}')
        for out, leaves in sorted(sl.items()):
            print(f'  {out} <- ' + ' '.join(leaves))
        for l in lines:   # the instructions that write an output lane, operands named by CTAB (derived, no bytes)
            parts = l.split(' ', 1)
            if len(parts) == 2 and not parts[0].startswith('dcl') and parts[1].split(',')[0].strip()[:2] in ('oT', 'oD', 'oC'):
                ops = [o.strip() for o in parts[1].split(',')]
                named = []
                for o in ops[1:]:
                    r = o.lstrip('-').split('.')[0]
                    nm = next((n for (rs, reg), (n, k) in names.items()
                               if r[:1] == 'c' and r[1:].isdigit() and rs == 2 and reg <= int(r[1:]) < reg + k), None)
                    named.append(nm + o[len(r) + (o[0] == '-'):] if nm else o)
                print(f'  write {ops[0]} = {parts[0]}(' + ', '.join(named) + ')')
        if a.listing:
            for l in lines:
                print('    ' + l)


if __name__ == '__main__':
    main()
