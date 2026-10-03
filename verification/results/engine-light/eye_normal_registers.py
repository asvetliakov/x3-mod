#!/usr/bin/env python3
"""Engine light (docs/architecture/engine-light.md): the eye-vector and normal inputs and the vertex row layouts.

For every reviewed original pair (verification/probe/run_linear_material.py PAIRS, 168) this walks the local original
programs (X3M_SHADER_PROGRAM_DIRECTORY, default /tmp/x3-shader-sweep/programs; never in the repository) with a small
SM3 token decoder and reports:
- the VS output carrying the eye vector cam - P (`mov r.x, cK.w` x3, `add r.xyz, -world, cam`, optionally `nrm`) and the
  one carrying the world normal (the g_mWorldIT dp4 of the input normal), by TEXCOORD index; whether the eye is
  normalised before interpolation;
- the PS input register each of those TEXCOORDs is declared on;
- the VS's world rows (the dp4 of the position) and view-inverse rows (the cK.w camera reads).
Prints one JSON object: per PS family the (eye, normal) input registers and the eye form, per VS the layout, and the
consistency verdict (every PS's pairs agree). verification/analysis/test_engine_light.py asserts the production
constants (v2 / v3, TEXCOORD1 / TEXCOORD2; layouts 28/34 and 7/13) against it.
"""
import collections
import json
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
PROGRAMS = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
USAGE = {0: 'position', 3: 'normal', 5: 'texcoord', 10: 'color'}


def words(name):
    data = (PROGRAMS / (name + '.bin')).read_bytes()
    return struct.unpack('<%dI' % (len(data) // 4), data)


def rtype(t):
    return ((t >> 28) & 7) | (((t >> 11) & 3) << 3)


def operand(t):
    kind = {0: 'r', 1: 'v', 2: 'c', 6: 'o'}.get(rtype(t), '?')
    return kind, t & 0x7ff


def instructions(w):
    at = 1
    while at < len(w):
        t = w[at]
        if t == 0xffff:
            return
        op, n = t & 0xffff, (t >> 24) & 15
        if op == 0xfffe:
            at += ((t >> 16) & 0x7fff) + 1
            continue
        yield op, w[at + 1:at + 1 + n]
        at += n + 1


def vertex(name):
    """Output index -> (usage, index); tags of the outputs; world and view-inverse registers."""
    w = words(name)
    outputs, inputs, tags, out = {}, {}, {}, {}
    world, camera = set(), set()
    for op, args in instructions(w):
        if op == 31:  # dcl
            kind, number = operand(args[1])
            (outputs if kind == 'o' else inputs)[number] = (USAGE.get(args[0] & 31, '?'), (args[0] >> 16) & 15)
            continue
        if op in (81, 27, 29, 38, 39, 40, 42, 43) or not args:
            continue
        kind, number = operand(args[0])
        lanes = (args[0] >> 16) & 15
        sources = [operand(a) for a in args[1:]]
        negated = [((a >> 24) & 15) == 1 for a in args[1:]]
        swizzle = [(a >> 16) & 0xff for a in args[1:]]
        tag = None
        if op == 1 and sources[0][0] == 'c' and swizzle[0] == 0xff:  # mov r.?, cK.wwww: the camera position
            tag = 'cam'
            camera.add(sources[0][1])
        elif op == 4 and sources[0][0] == 'v':  # mad rX, vN.xyzx, literal: position or normal with w = 1
            tag = {'position': 'pos', 'normal': 'nin'}.get(inputs.get(sources[0][1], ('',))[0])
        elif op == 9 and sources[0][0] == 'r' and tags.get(sources[0][1]) == 'pos' and sources[1][0] == 'c':
            tag = 'wpos'
        elif op == 9 and sources[0][0] == 'r' and tags.get(sources[0][1]) == 'nin' and sources[1][0] == 'c':
            tag = 'normal'
        elif op == 2 and len(sources) == 2 and all(s[0] == 'r' for s in sources):
            a, b = tags.get(sources[0][1]), tags.get(sources[1][1])
            if (negated[0] and a == 'wpos' and b == 'cam') or (negated[1] and b == 'wpos' and a == 'cam'):
                tag = 'eye'
        elif op == 36 and sources[0][0] == 'r' and tags.get(sources[0][1]) == 'eye':
            tag = 'eye_n'
        elif op == 1 and sources[0][0] == 'r' and tags.get(sources[0][1]) in ('eye', 'eye_n', 'normal'):
            tag = tags.get(sources[0][1])
        if tag == 'wpos' or (op == 9 and tag == 'pos'):
            pass
        if op == 9 and sources[0][0] == 'r' and tags.get(sources[0][1]) == 'pos' and kind == 'r':
            world.add(sources[1][1])
        if kind == 'o':
            if tag in ('eye', 'eye_n', 'normal') and lanes == 7:
                out[outputs[number]] = tag
            continue
        if tag in ('wpos', 'normal', 'cam') and tags.get(number) == tag:
            continue
        if tag:
            tags[number] = tag
        elif lanes & 7:
            tags.pop(number, None)
    return out, sorted(world), sorted(camera)


def pixel(name):
    result = {}
    for op, args in instructions(words(name)):
        if op == 31 and operand(args[1])[0] == 'v' and (args[0] & 31) == 5:
            result[(args[0] >> 16) & 15] = operand(args[1])[1]
    return result


def main():
    import run_linear_material as runner
    if not all((PROGRAMS / (f'vs_{vs}.bin')).is_file() and (PROGRAMS / (f'ps_{ps}.bin')).is_file() for vs, ps in runner.PAIRS):
        print(json.dumps(dict(available=False, programs=str(PROGRAMS))))
        return
    per_ps = collections.defaultdict(set)
    family_of, layouts, eye_forms = {}, {}, collections.defaultdict(set)
    for index, (vs, ps) in enumerate(runner.PAIRS):
        family = ('hull' if index < 110 else 'asteroid' if index < 116 else 'palette' if index < 148 else 'xt' if index < 162 else 'glass')
        family_of[ps] = family
        out, world, camera = vertex('vs_' + vs)
        layouts[vs] = dict(world=world, view_inverse=camera)
        eye = [k for k, v in out.items() if v in ('eye', 'eye_n')]
        normal = [k for k, v in out.items() if v == 'normal']
        inputs = pixel('ps_' + ps)
        e = eye[0][1] if len(eye) == 1 else None
        n = normal[0][1] if len(normal) == 1 else None
        per_ps[ps].add((e, n, inputs.get(e), inputs.get(n)))
        if eye:
            eye_forms[ps].add(out[eye[0]])
    rows = {}
    for ps, keys in per_ps.items():
        (e, n, ve, vn), = keys if len(keys) == 1 else ((None, None, None, None),)
        rows[ps] = dict(family=family_of[ps], consistent=len(keys) == 1, eye_texcoord=e, normal_texcoord=n, eye_input=ve,
                        normal_input=vn, eye_forms=sorted(eye_forms[ps]))
    census = collections.Counter((r['family'], r['eye_texcoord'], r['normal_texcoord'], r['eye_input'], r['normal_input'],
                                  tuple(r['eye_forms'])) for r in rows.values())
    print(json.dumps(dict(available=True, pairs=len(runner.PAIRS), pixel_programs=len(rows),
                          inconsistent=sorted(ps for ps, r in rows.items() if not r['consistent']),
                          census=[dict(family=k[0], eye_texcoord=k[1], normal_texcoord=k[2], eye_input=k[3], normal_input=k[4],
                                       eye_forms=list(k[5]), programs=v) for k, v in sorted(census.items(), key=str)],
                          rows=rows, layouts=layouts), indent=1, sort_keys=True))


if __name__ == '__main__':
    main()
