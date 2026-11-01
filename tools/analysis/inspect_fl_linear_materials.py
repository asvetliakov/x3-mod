#!/usr/bin/env python3
"""Derive the FL hull material rows of the original-shading variants (part 4b).

Offline only: reads the local FL pixel programs of the FL motion extension
(`verification/results/fl-shader-coverage/motion-output-profiles-fl.json` and
its generated include) and derives, per program, the exact sites the production
transformer pins (`linear_material.cpp` pixel_sites / xt_pixel_sites,
lightmap_term, the diffuse join of the strength twin, the hull emitter tail of
`linear_emission.cpp`). It writes only derived numbers: DWORD offsets,
register numbers, CTAB-derived register roles. Never shader words, literals or
disassembly.

Outputs (all FL-only; the AP tables and includes are never written):
  src/renderer/linear_material_pixels_fl_inc.h   conventional Pixel rows
  src/renderer/linear_material_pairs_fl_inc.h    their exact (VS, PS) pairs
  src/renderer/linear_xt_profiles_fl_inc.h       XtPixel rows + FL register roles
  src/renderer/material_exposure_profiles_fl_inc.h  diffuse joins (strength twin)
  src/renderer/linear_emission_hull_fl_inc.h     HDR additive hull-gain rows
and a compact JSON report. The generator refuses (writes nothing) when an FL
row would collide with an AP row of the same table, when the AP base files it
reads differ from the ones the report binds, or when any derivation is
ambiguous. The production code re-proves every row at create time (fail
closed), and verification/results/fl-shader-coverage/linear_variants.py
exercises every derived row through it.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inspect_motion_output_profiles as shader  # noqa: E402
from index_shaders import fnv1a64  # noqa: E402
from shader_constants import parse_ctab  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / 'src/renderer'
OUTPUTS = {'pixel': 'linear_material_pixels_fl_inc.h', 'pair': 'linear_material_pairs_fl_inc.h',
           'xt': 'linear_xt_profiles_fl_inc.h', 'seed': 'material_exposure_profiles_fl_inc.h',
           'hull': 'linear_emission_hull_fl_inc.h'}
# AP tables the FL rows sit beside: read for collisions, never written.
AP_BASE = ('src/renderer/linear_material.cpp', 'src/renderer/linear_xt_profiles_inc.h',
           'src/renderer/material_exposure_profiles_inc.h', 'src/renderer/linear_emission.cpp')
MOV, ADD, MAD, MUL, DP3, DP4, LRP, TEXLD = 1, 2, 4, 5, 8, 9, 18, 66
IF, IFC, ELSE, ENDIF = 40, 41, 42, 43
FLOW = {27, 28, 29, 30, 38, 39, 40, 41, 42, 43, 44, 45, 25, 26}
SCALAR = {6, 7, 14, 15, 32, 37}  # rcp rsq exp log pow sincos: one replicated source lane
SAT, PP = 0x100000, 0x200000
# The hull emitter census (docs/reverse-engineering/effect-shader-users.md,
# "Additive emitters drawn by material programs"): ONE/ONE materials of the
# effect sources XT_standard_lighting.fx and standard_lighting.fx. A program
# belongs when an effect basename that binds it is one of these sources or its
# two-sided `2s` form (a program bound only by a `_damage` effect is not). The
# same rule over the AP result reproduces the twelve AP rows exactly (checked).
HULL_STEMS = {'standard_lighting', 'xt_standard_lighting'}
AP_HULL = {'5f82ecacd39529cd', '6733b119142c8d42', 'fffdabd910793aba', '496049cec2066ed3', 'e6794b6ec37ff71a',
           'f1b0e820c7b488c3', '0c1f3f0f440e4a0c', '7c83ed50c9894e44', '99153c144030c396', '64bac8bb307eb896',
           'c1452981fd0bff64', 'e70adc744a38ca59'}


def check(condition, reason):
    if not condition:
        raise ValueError(reason)


def stem(basename):
    return re.sub(r'(_damage)?(2s)?$', '', basename)


def hull_program(basenames):
    return any(re.sub(r'2s$', '', name) in HULL_STEMS for name in basenames)


class Program:
    """Decoded instruction list with per-operand DWORD positions."""

    def __init__(self, name, code):
        self.name, self.code = name, code
        self.words, items, _ = shader.instructions(code)
        check(self.words[0] == 0xffff0300, name + ': not ps_3_0')
        self.ctab = {entry['name']: entry for entry in parse_ctab(code)}
        self.rows = []
        for item in items:
            op, w = item['opcode'], item['words']
            row = {'at': item['dword'], 'op': op, 'n': item['length'], 'dst': None, 'src': []}
            if op in (31, 81):
                row['header'] = True
                self.rows.append(row)
                continue
            first = 0 if op in FLOW or not w else 1
            if first:
                row['dst'] = self.reg(w[0])
            for k, token in enumerate(w[first:]):
                operand = self.reg(token)
                operand['dword'] = item['dword'] + 1 + first + k
                operand['swizzle'] = shader.swizzle_of(token)
                operand['modifier'] = (token >> 24) & 15
                operand['token'] = token
                check(not token & shader.RELATIVE, name + ': relative addressing')
                row['src'].append(operand)
            self.rows.append(row)
        self.at = {row['at']: row for row in self.rows}

    @staticmethod
    def reg(token):
        kind, number = shader.register_of(token)
        return {'kind': kind, 'number': number, 'mask': shader.mask_of(token), 'token': token,
                'sat': bool(token & SAT), 'pp': bool(token & PP)}

    def constant(self, name):
        entry = self.ctab.get(name)
        check(entry is not None and entry['register_set'] == 2 and entry['count'] == 1, f'{self.name}: CTAB {name}')
        return entry['register']

    def sampler(self, name):
        entry = self.ctab.get(name)
        return None if entry is None else entry['register']

    def body(self):
        return [row for row in self.rows if not row.get('header')]


def source_lanes(row, k):
    """Lanes of source k that a component of the result reads, per result lane."""
    op, swizzle = row['op'], row['src'][k]['swizzle']
    mask = row['dst']['mask'] if row['dst'] else 'xyzw'
    if op == DP3:
        return {lane: set(swizzle[:3]) for lane in mask}
    if op == DP4:
        return {lane: set(swizzle) for lane in mask}
    if op == 36:  # nrm
        return {lane: set(swizzle[:3]) for lane in mask}
    if op in SCALAR:
        return {lane: {swizzle[0]} for lane in mask}
    if op == TEXLD:
        return {lane: (set(swizzle) if k == 0 else set()) for lane in mask}
    return {lane: {swizzle['xyzw'.index(lane)]} for lane in mask}


def operand_name(operand):
    return {0: 'r', 1: 'v', 2: 'c', 10: 's'}.get(operand['kind'], 't%d_' % operand['kind']) + str(operand['number'])


def dataflow(program, seed, transfer=None):
    """Forward per-lane set propagation with IF/ELSE/ENDIF union. `seed(operand)`
    gives the initial set of a non-temporary operand; `transfer(row, sets)` may
    add sets to a result (texture fetches). Returns {at: [per-source sets]}."""
    state, stack, seen = {}, [], {}
    for row in program.body():
        op = row['op']
        if op in (IF, IFC):
            stack.append([dict(state), None])
            continue
        if op == ELSE:
            stack[-1][1] = state
            state = dict(stack[-1][0])
            continue
        if op == ENDIF:
            before, arm = stack.pop()
            other = arm if arm is not None else before
            state = {key: state.get(key, frozenset()) | other.get(key, frozenset())
                     for key in set(state) | set(other)}
            continue
        check(op not in FLOW, program.name + ': loop/subroutine flow')
        if not row['dst']:
            continue
        per_source = []
        result = {lane: set() for lane in row['dst']['mask']}
        for k, operand in enumerate(row['src']):
            name = operand_name(operand)
            values = set()
            for lane, read in source_lanes(row, k).items():
                for component in read:
                    got = state.get((name, component), frozenset()) if operand['kind'] == 0 else seed(operand)
                    result[lane] |= got
                    values |= got
            per_source.append(values)
        if transfer:
            for lane, extra in transfer(row).items():
                result[lane] |= extra
        seen[row['at']] = per_source
        destination = operand_name(row['dst'])
        for lane, values in result.items():
            state[(destination, lane)] = frozenset(values)
    check(not stack, program.name + ': unbalanced flow')
    return seen


def flow_depth(program):
    depth, result = 0, {}
    for row in program.body():
        if row['op'] in (ENDIF,):
            depth -= 1
        result[row['at']] = depth
        if row['op'] in (IF, IFC):
            depth += 1
    return result


def textures(program):
    found = {}
    for row in program.body():
        if row['op'] == TEXLD:
            stage = row['src'][1]['number']
            check(stage not in found, f'{program.name}: two fetches of s{stage}')
            found[stage] = row
    return found


def outputs(program):
    rows = [row for row in program.body() if row['dst'] and row['dst']['kind'] == 8]
    final = [row for row in rows if row['dst']['mask'] == 'xyz']
    alpha = [row for row in rows if row['dst']['mask'] == 'w']
    check(len(rows) == 2 and len(final) == len(alpha) == 1, program.name + ': oC0 inventory')
    return final[0], alpha[0]


def light_uses(program, light0, light1):
    uses = []
    for row in program.body():
        for operand in row['src']:
            if operand['kind'] == 2 and operand['number'] in (light0, light1):
                check(row['op'] in (MAD, MUL) and row['dst']['mask'] == 'xyz' and operand['swizzle'] == 'xyzw'
                      and operand['modifier'] == 0, program.name + ': light colour use shape')
                uses.append((operand['dword'], operand['number']))
    check(Counter(value for _, value in uses) == {light0: 2, light1: 2}, program.name + ': light colour use count')
    return uses


def tags_seed(program):
    names = {}
    for name, entry in program.ctab.items():
        names[(entry['register_set'], entry['register'])] = name

    def seed(operand):
        if operand['kind'] == 2:
            name = names.get((2, operand['number']))
            return frozenset({name}) if name else frozenset()
        if operand['kind'] == 1:
            return frozenset({'v%d' % operand['number']})
        return frozenset()

    def transfer(row):
        if row['op'] != TEXLD:
            return {}
        name = names.get((3, row['src'][1]['number']), 's%d' % row['src'][1]['number'])
        return {lane: {name} for lane in row['dst']['mask']}
    return seed, transfer


def diffuse_join(program):
    """The MAD that adds the specular term to the diffuse lobes (the strength
    twin's join; exposure seed): both sides carry both light colours, the
    specular texture enters exactly one side, nothing from the cube or light-map
    textures or COLOR0 enters it (the XT bump normal reads the diffuse fetch). product: the diffuse lobes are the
    MAD's product (a * b), else its addend."""
    seed, transfer = tags_seed(program)
    flow = dataflow(program, seed, transfer)
    lights = {'LightDir_Color0', 'LightDir_Color1'}
    forbidden = {'CubeMapTexSampler', 'LightMapTexSampler', 'v0'}
    joins = []
    for row in program.body():
        if row['op'] != MAD or row['dst']['mask'] != 'xyz' or row['at'] not in flow:
            continue
        a, b, c = flow[row['at']]
        product, addend = a | b, c
        if not (lights <= product and lights <= addend) or (product | addend) & forbidden:
            continue
        if ('SpecularTexSampler' in product) == ('SpecularTexSampler' in addend):
            continue
        joins.append({'at': row['at'], 'lanes': 7, 'product': 'SpecularTexSampler' in addend,
                      'depth': flow_depth(program)[row['at']]})
    check(len(joins) == 1, program.name + ': diffuse join ambiguous')
    check(joins[0]['depth'] == 0, program.name + ': diffuse join inside flow control')
    return joins[0]


def rgb_sites(program, base_at, base_reg, colour_constants, lightmap_stage, cube_stage, final_at):
    """Full-precision RGB destinations: every instruction whose result depends
    on COLOR0 RGB, a light/palette colour, the completed albedo (from base_at)
    or the light-map/cube RGB; each must be an xyz MOV/ADD/MUL/MAD."""
    taint = {('v0', c) for c in 'xyz'} | {('c%d' % r, c) for r in colour_constants for c in 'xyz'}
    stack, sites = [], []
    for row in program.body():
        op = row['op']
        if row['at'] == base_at:
            taint |= {('r%d' % base_reg, c) for c in 'xyz'}
        if op in (IF, IFC):
            stack.append((set(taint), None))
            continue
        if op == ELSE:
            before, _ = stack[-1]
            stack[-1] = (before, set(taint))
            taint = set(before)
            continue
        if op == ENDIF:
            before, other = stack.pop()
            taint |= before if other is None else other
            continue
        if not row['dst']:
            continue
        depends = any((operand_name(operand), component) in taint
                      for k, operand in enumerate(row['src'])
                      for read in source_lanes(row, k).values() for component in read)
        destination = operand_name(row['dst'])
        for lane in row['dst']['mask']:
            taint.discard((destination, lane))
        if depends:
            check(op in (MOV, ADD, MUL, MAD) and row['dst']['mask'] == 'xyz' and row['dst']['pp'],
                  f'{program.name}: RGB path crosses a non-colour instruction at {row["at"]}')
            sites.append(row['at'])
            taint |= {(destination, c) for c in 'xyz'}
        if op == TEXLD and row['src'][1]['number'] in (lightmap_stage, cube_stage):
            taint |= {(destination, c) for c in 'xyz'}
    check(not stack and final_at in sites, program.name + ': RGB path misses the final colour')
    return sites


def conventional_row(program, pair, motion_row):
    bump = pair['effects']['techniques'][0] in ('BUMPMAP', 'BUMPMAP_LOW')
    check(set(pair['effects']['techniques']) <= ({'BUMPMAP', 'BUMPMAP_LOW'} if bump else {'DEFAULT'}),
          program.name + ': technique')
    fetch = textures(program)
    check(sorted(fetch) == list(range(5 if bump else 4)), program.name + ': sampler set')
    lightmap, cube = (3, 4) if bump else (2, 3)
    check(program.sampler('LightMapTexSampler') == lightmap and program.sampler('CubeMapTexSampler') == cube and
          program.sampler('DiffuseTexSampler') == 0, program.name + ': sampler roles')
    final, alpha = outputs(program)
    check(alpha['at'] == final['at'] + 4, program.name + ': alpha after the final colour')
    clamps = [row for row in program.body() if row['op'] == MOV and row['src'][0]['kind'] == 1 and
              row['src'][0]['number'] == 0 and row['src'][0]['swizzle'] == 'xyzw' and row['dst']['sat']]
    check(len(clamps) == 1 and clamps[0]['dst']['mask'] == 'xyz' and clamps[0]['dst']['kind'] == 0,
          program.name + ': COLOR0 clamp')
    clamp = clamps[0]
    light0, light1 = program.constant('LightDir_Color0'), program.constant('LightDir_Color1')
    glow = program.constant('g_EnableGlow')
    blend = program.at.get(fetch[lightmap]['at'] + 4)
    check(blend and blend['op'] == LRP and blend['src'][0]['number'] == glow, program.name + ': glow LRP')
    # The completed affine albedo: three DP4 rows writing albedo.x/.y/.z in order
    # from the graded colour with consecutive matrix constants.
    albedo, source = (4, 3) if bump else (3, 2)
    rows = [row for row in program.body() if row['op'] == DP4 and row['dst']['kind'] == 0 and
            row['dst']['number'] == albedo and row['src'][0]['kind'] == 0 and row['src'][0]['number'] == source]
    check(len(rows) == 3 and [row['dst']['mask'] for row in rows] == ['x', 'y', 'z'] and
          [row['at'] for row in rows] == [rows[0]['at'] + 4 * k for k in range(3)], program.name + ': affine rows')
    first = rows[0]['src'][1]['number']
    check([row['src'][1]['number'] for row in rows] == [first, first + 1, first + 2], program.name + ': matrix rows')
    affine_end = rows[2]['at']
    uses = light_uses(program, light0, light1)
    colours = sorted(o for o, v in uses if v == light0) + sorted(o for o, v in uses if v == light1)
    rgb = rgb_sites(program, affine_end + 4, albedo, (light0, light1), lightmap, cube, final['at'])
    check(len(rgb) <= 24, program.name + ': more than 24 RGB sites')
    return {'family': 'conventional', 'bump': bump, 'textures': [fetch[s]['at'] for s in sorted(fetch)],
            'affine_end': affine_end, 'affine_first': first, 'clamp': clamp['at'],
            'clamp_temporary': clamp['dst']['number'], 'final_rgb': final['at'], 'alpha': alpha['at'],
            'light0': light0, 'light1': light1, 'glow': glow, 'color_source': colours, 'rgb': rgb,
            'lightmap_stage': lightmap, 'lightmap_fetch': fetch[lightmap]['at'],
            'depth_texcoord_override': 0 if motion_row['depth_texcoord_index'] == (6 if bump else 5)
            else motion_row['depth_texcoord_index'],
            'temporary_base': motion_row['pixel_temporary_base']}


def xt_row(program, pair, motion_row):
    bump = pair['effects']['techniques'][0] in ('BUMPMAP', 'BUMPMAP_LOW')
    terra = all(stem(name) == 'xt_terraformer' for name in pair['effects']['basenames'])
    check(terra or all(stem(name) == 'xt_standard_lighting' for name in pair['effects']['basenames']),
          program.name + ': XT effect')
    fetch = textures(program)
    check(sorted(fetch) == list(range(7 if bump else 5)), program.name + ': sampler set')
    lightmap, cube = (3, 4) if bump else (2, 3)
    check(program.sampler('LightMapTexSampler') == lightmap and program.sampler('CubeMapTexSampler') == cube,
          program.name + ': sampler roles')
    for row in fetch.values():
        check(row['dst']['kind'] == 0 and row['dst']['mask'] == 'xyzw', program.name + ': fetch destination')
    final, alpha = outputs(program)
    light0, light1 = program.constant('LightDir_Color0'), program.constant('LightDir_Color1')
    names = ('g_Color_1', 'g_Color_2', 'g_Color_3', 'g_Color_Lines', 'g_Color_Highlight')
    palette_registers = [program.constant(name) for name in names]
    palette_first = min(palette_registers)
    check(sorted(palette_registers) == list(range(palette_first, palette_first + 5)), program.name + ': palette run')
    palette = []
    for row in program.body():
        for operand in row['src']:
            if operand['kind'] == 2 and palette_first <= operand['number'] < palette_first + 5:
                check(operand['swizzle'] == 'xyzw' and row['dst']['mask'] == 'xyz', program.name + ': palette use')
                palette.append((operand['dword'], operand['number']))
    check(sorted(v for _, v in palette) == sorted(palette_registers), program.name + ': palette use count')
    uses = light_uses(program, light0, light1)
    clamps = [row['at'] for row in program.body() if row['op'] == MOV and row['src'][0]['kind'] == 1 and
              row['src'][0]['number'] == 0 and row['dst']['sat'] and row['dst']['pp'] and
              row['dst']['mask'] == 'xyz' and row['src'][0]['swizzle'] == 'xyzw']
    check(len(clamps) == 2, program.name + ': two COLOR0 clamps')
    carrier = 7 if bump else 5
    scalar = [(operand['dword'], 'xyzw'.index(operand['swizzle'][0])) for row in program.body()
              for operand in row['src'] if operand['kind'] == 1 and operand['number'] == carrier]
    check(len(scalar) == 2 and {lane for _, lane in scalar} == {0, 1}, program.name + ': scalar transport')
    # Completed native base (the converted path's cut; the original variants
    # only require an instruction boundary): after the decal branch for the
    # standard XT programs, after the affine rows for terraformer.
    if terra:
        rows = [row for row in program.body() if row['op'] == DP4 and row['src'][1]['kind'] == 2 and
                row['src'][1]['number'] in (1, 2, 3)]
        check(len(rows) == 3 and len({row['dst']['number'] for row in rows}) == 1, program.name + ': affine rows')
        base_at = max(row['at'] for row in rows) + 4
        base_reg = rows[0]['dst']['number']
    else:
        entry = program.ctab.get('g_bIsDecalMap')
        check(entry is not None and entry['register_set'] == 0, program.name + ': decal flag')
        decal = entry['register']
        opener = next(row for row in program.body() if row['op'] == IF and row['src'][0]['kind'] == 14 and
                      row['src'][0]['number'] == decal)
        alternate = next(row for row in program.body() if row['at'] > opener['at'] and row['op'] == ELSE)
        closing = next(row for row in program.body() if row['at'] > opener['at'] and row['op'] == ENDIF)
        move = program.at[alternate['at'] + 1]
        check(move['op'] == MOV and move['dst']['mask'] == 'xyz', program.name + ': decal alternate base')
        base_at, base_reg = closing['at'] + 1, move['dst']['number']
    check(base_at in program.at, program.name + ': base cut not a boundary')
    rgb = rgb_sites(program, base_at, base_reg, (light0, light1, *palette_registers), lightmap, cube, final['at'])
    check(len(rgb) <= 27, program.name + ': more than 27 RGB sites')
    return {'family': 'xt', 'bump': bump, 'terra': terra, 'base_at': base_at, 'base_reg': base_reg,
            'final_rgb': final['at'], 'alpha': alpha['at'],
            'textures': [fetch[s]['at'] for s in sorted(fetch)],
            'texture_registers': [fetch[s]['dst']['number'] for s in sorted(fetch)],
            'lights': sorted(uses), 'palette': sorted(palette), 'clamps': clamps, 'scalar': scalar, 'rgb': rgb,
            'light0': light0, 'light1': light1, 'palette_first': palette_first,
            'lightmap_stage': lightmap, 'lightmap_fetch': fetch[lightmap]['at'],
            'temporary_base': motion_row['pixel_temporary_base']}


def hull_row(program, family):
    first = next(row['at'] for row in program.rows if row['op'] in (31, 81))
    final, _ = outputs(program)
    modulated = family == 'xt'
    check(final['op'] == (MAD if modulated else ADD), program.name + ': hull emitter final colour')
    return {'definition': first, 'emission': final['at'], 'modulated': modulated}


def motion_rows(path):
    """MotionOutputProfile rows of a generated include: {ps: fields}."""
    fields = [name.split('[')[0] for name in shader.HEADER_FIELDS]
    rows = {}
    text = path.read_text()
    starts = [m.start() for m in re.finditer(r'^\{0x[0-9a-f]{16}ull,', text, re.M)]
    for begin, end in zip(starts, starts[1:] + [len(text)]):
        block = text[begin + 1:end].strip().rstrip(',').rstrip('}')
        values = [v.strip() for v in re.sub(r'\{[^{}]*\}', 'LIST', block).split(',')]
        check(len(values) == len(fields), 'motion row field count')
        record = dict(zip(fields, values))
        rows[record['pixel_fingerprint'][2:18]] = {key: int(record[key]) for key in
                                                   ('pixel_temporary_base', 'depth_texcoord_index',
                                                    'pixel_input_register', 'pixel_dword_count')}
    return rows


def ap_hashes():
    """Every 64-bit literal of the AP tables beside the FL rows (collision guard)."""
    found = set()
    for name in AP_BASE:
        text = (ROOT / name).read_text()
        text = re.sub(r'#include "[a-z_]*_fl_inc\.h"', '', text)
        found |= set(re.findall(r'0x([0-9a-f]{16})ull', text))
    return found


def array(values):
    return '{' + ','.join(str(int(v)) for v in values) + '}'


def uses(values):
    return '{{' + ','.join('{%d,%d}' % pair for pair in values) + '}}'


HEADER = ('// Generated by tools/analysis/inspect_fl_linear_materials.py; derived metadata only.\n'
          '// FL extension (docs/architecture/farnhams-legacy-shaders.md, part 4b): rows of the\n'
          '// FL-only hull programs, included after the AP rows of the same table; the AP\n'
          '// rows are unchanged and these are inert in AP (it never creates the programs).\n'
          '// FL program inventory SHA256: {inventory}\n')


def render(report):
    head = HEADER.format(inventory=report['inventory_sha256'])
    pixel, pair, xt, seed, hull = [], [], [], [], []
    for p in report['programs']:
        key = '0x%sull' % p['ps']
        row = p['row']
        seed.append('    {%s,%d,%d,%s},' % (key, p['join']['at'], p['join']['lanes'], str(p['join']['product']).lower()))
        if p['hull_emitter']:
            h = p['hull_emitter']
            hull.append('    {%s,%d,%d,%d,%s},' % (key, p['words'], h['definition'], h['emission'],
                                                   str(h['modulated']).lower()))
        if row['family'] == 'conventional':
            textures = row['textures'] + [0] * (5 - len(row['textures']))
            pixel.append('    {%s,%d,%s,%d,%d,%d,%d,%d,%d,\n     %s,%s,%s,%d,0,0,0,%d,%d,%d,true},' % (
                key, p['words'], array(textures), row['affine_end'], row['clamp'], row['final_rgb'],
                row['clamp_temporary'], row['light0'], row['light1'], array(row['color_source']), array(row['rgb']),
                str(row['bump']).lower(), row['depth_texcoord_override'], row['glow'], row['affine_first'],
                row['temporary_base']))
            pair.append('    {0x%sull,%s,%s},' % (p['vs'], key, '0x1f' if row['bump'] else '0x0f'))
        else:
            textures = row['textures'] + [0] * (7 - len(row['textures']))
            registers = row['texture_registers'] + [0] * (7 - len(row['texture_registers']))
            xt.append('    {{%s,%d,%s,false,%s,%d,%d,%d,%d,\n     %s,%s,\n     %s,%s,%s,%s,\n     %s},\n'
                      '     {%d,%d,%d,%d}},' % (
                          key, p['words'], str(row['bump']).lower(), str(row['terra']).lower(), row['base_at'],
                          row['base_reg'], row['final_rgb'], row['alpha'], array(textures), array(registers),
                          uses(row['lights']), uses(row['palette']), array(row['clamps']), uses(row['scalar']),
                          array(row['rgb']), row['light0'], row['light1'], row['palette_first'],
                          row['temporary_base']))
    note = {
        'pixel': '// Pixel rows (linear_material.cpp): field order of `struct Pixel`, then the FL\n'
                 '// fields glow, affine_first, temporary_base and original_only.\n',
        'pair': '// Pair rows (linear_material.cpp `pairs[]`): VS, PS, disabled-sRGB sampler mask.\n',
        'xt': '// XtPixelFl rows (linear_xt_material_inc.h): an XtPixel row, then the FL register\n'
              '// roles light0, light1, palette_first and the temporary base.\n',
        'seed': '// ExposureSeed rows (material_exposure_profiles_inc.h): the diffuse join.\n',
        'hull': '// HullProfile rows (linear_emission.cpp): words, first DEF/DCL, final colour, XT.\n'}
    bodies = {'pixel': pixel, 'pair': pair, 'xt': xt, 'seed': seed, 'hull': hull}
    return {OUTPUTS[k]: head + note[k] + '\n'.join(v) + '\n' for k, v in bodies.items()}


def derive(arguments):
    extension = json.loads(arguments.motion.read_text())
    motion = motion_rows(arguments.motion_header)
    manifest = {p['id']: p for p in json.loads(arguments.manifest.read_text())['programs']}
    base_hashes = ap_hashes()
    pairs = sorted(extension['pairs'], key=lambda pair: pair['ps'])
    check(len(pairs) == len({pair['ps'] for pair in pairs}), 'one pair per FL pixel program expected')
    programs = []
    for pair in pairs:
        name = 'ps_' + pair['ps']
        code = (arguments.programs / (name + '.bin')).read_bytes()
        check(fnv1a64(code) == pair['ps'] and hashlib.sha256(code).hexdigest() == manifest[name]['sha256'],
              name + ': local program differs from the inventory')
        check(pair['ps'] not in base_hashes, name + ': collides with an AP row (refused: an AP row would change)')
        program = Program(name, code)
        family = 'xt' if any(b.startswith('xt_') for b in pair['effects']['basenames']) else 'conventional'
        row = (xt_row if family == 'xt' else conventional_row)(program, pair, motion[pair['ps']])
        check(motion[pair['ps']]['pixel_dword_count'] == len(program.words), name + ': motion row length')
        record = {'ps': pair['ps'], 'vs': pair['vs'], 'words': len(program.words),
                  'basenames': pair['effects']['basenames'], 'techniques': pair['effects']['techniques'],
                  'row': row, 'join': diffuse_join(program),
                  'hull_emitter': hull_row(program, family) if hull_program(pair['effects']['basenames']) else None,
                  'lightmap_flow_depth': flow_depth(program)[row['lightmap_fetch']]}
        check(record['lightmap_flow_depth'] == 0, name + ': light-map fetch inside flow control')
        programs.append(record)
    # The hull emitter rule reproduces the twelve AP rows over the AP result.
    base = json.loads(arguments.base_motion.read_text())
    ap_rule = {p['ps'] for p in base['pairs'] if p.get('ps_model') == '3_0' and
               hull_program(p['effects']['basenames'])}
    check(ap_rule == AP_HULL, 'the hull emitter rule does not reproduce the AP rows')
    counts = Counter(p['row']['family'] for p in programs)
    report = {
        'schema': 1,
        'scope': 'Derived FL hull material rows of the original-shading variants (part 4b). Offsets are '
                 'original DWORDs; registers from CTAB and the instruction stream. Derived numbers only.',
        'producer': 'tools/analysis/inspect_fl_linear_materials.py',
        'tool_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'inventory_sha256': extension['inventory_sha256'],
        'motion_extension_sha256': hashlib.sha256(arguments.motion.read_bytes()).hexdigest(),
        'ap_base_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in AP_BASE
                           if not name.endswith('.cpp')},
        'counts': {'programs': len(programs), 'conventional': counts['conventional'], 'xt': counts['xt'],
                   'pairs': counts['conventional'], 'exposure_seeds': len(programs),
                   'hull_emitters': sum(bool(p['hull_emitter']) for p in programs),
                   'join_product': sum(p['join']['product'] for p in programs)},
        'hull_emitter_rule': {'stems': sorted(HULL_STEMS), 'reproduces_ap_rows': len(ap_rule)},
        'programs': programs,
    }
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--programs', type=Path, default=Path('/tmp/x3-fl-shader-sweep/programs'))
    parser.add_argument('--manifest', type=Path, default=Path('/tmp/x3-fl-shader-sweep/manifest.json'))
    parser.add_argument('--motion', type=Path,
                        default=ROOT / 'verification/results/fl-shader-coverage/motion-output-profiles-fl.json')
    parser.add_argument('--motion-header', type=Path, default=RENDERER / 'motion_output_profiles_fl_inc.h')
    parser.add_argument('--base-motion', type=Path, default=ROOT / 'verification/results/motion-output-profiles.json')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'verification/results/fl-shader-coverage/linear-materials-fl.json')
    parser.add_argument('--emit-headers', action='store_true', help='write the five FL includes')
    parser.add_argument('--check', action='store_true', help='compare instead of writing; exit 1 on a difference')
    arguments = parser.parse_args()
    report = derive(arguments)
    files = {arguments.output: json.dumps(report, indent=1) + '\n'}
    if arguments.emit_headers or arguments.check:
        files.update({RENDERER / name: text for name, text in render(report).items()})
    for path in files:
        check(path.name.endswith('_fl_inc.h') or path.suffix == '.json', 'refusing to write ' + str(path))
    stale = [str(path) for path, text in files.items() if not path.is_file() or path.read_text() != text]
    if arguments.check:
        print(json.dumps({'stale': stale}))
        return 1 if stale else 0
    for path, text in files.items():
        path.write_text(text)
    print(json.dumps(report['counts']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
