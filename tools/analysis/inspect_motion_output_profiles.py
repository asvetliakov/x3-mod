#!/usr/bin/env python3
"""Derive per-program motion-output insertion facts for the archive's material pairs.

This reads local D3D9 bytecode and exports only derived structure: hashes,
DWORD offsets, register numbers, counts and decoded operand fields. It never
writes shader words, literal values, comment/CTAB payloads or disassembly text.

Coverage is archive-wide: every vertex/pixel pairing bound by a technique pass
of the installed compiled effects (`effect_passes.py`) is classified. SM3 pairs
receive a transformation class (A, B, C) or an explicit unsupported reason and
the transformable ones become rows of the generated table; SM2 pairs receive a
feasibility record against the ps_2_0 / ps_2_x limits (no rows); SM1 pairs are
counted as unsupported (ps_1_x has no second color target). Captured draw
counts, when a capture log is supplied, are optional metadata per pair (zero
for pairs the capture never drew) and only order the rows.

The facts answer one narrow question per program: *where* would the reviewed
same-draw motion transformation splice, and *which* registers are unused. It is
not an eligibility decision. Object identity, vertex content, draw state, MRT
capability and the runtime relative-light bound remain separate gates.

Offsets are zero-based DWORD indices from the version token and include all
comments, matching `src/renderer/material_motion.cpp` and
`docs/reverse-engineering/motion-output-candidate.md`.
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
from index_shaders import fnv1a64, shader_end, SM1_ARITY  # noqa: E402

COMMENT = 0xfffe
END = 0x0000ffff
DEF, DEFB, DEFI, DCL = 0x51, 0x2f, 0x30, 0x1f
DEFINITIONS = {DEF: 'def', DEFB: 'defb', DEFI: 'defi'}
HEADER_OPCODES = set(DEFINITIONS) | {DCL}
LANES = 'xyzw'

# D3DSHADER_PARAM_REGISTER_TYPE; only the names this analysis needs are spelled.
REGISTER_TYPES = {0: 'r', 1: 'v', 2: 'c', 3: 'a', 4: 'rastout', 5: 'attrout',
                  6: 'o', 7: 'i', 8: 'oC', 9: 'oDepth', 10: 's', 11: 'c2',
                  12: 'c3', 13: 'c4', 14: 'b', 15: 'aL', 16: 'rh', 17: 'misc',
                  18: 'label', 19: 'p'}
CONSTANT_TYPES = {2, 11, 12, 13}
USAGES = {0: 'position', 1: 'blendweight', 2: 'blendindices', 3: 'normal',
          4: 'psize', 5: 'texcoord', 6: 'tangent', 7: 'binormal',
          8: 'tessfactor', 9: 'positiont', 10: 'color', 11: 'fog',
          12: 'depth', 13: 'sample'}
OPCODES = {0: 'nop', 1: 'mov', 2: 'add', 3: 'sub', 4: 'mad', 5: 'mul', 6: 'rcp',
           7: 'rsq', 8: 'dp3', 9: 'dp4', 10: 'min', 11: 'max', 12: 'slt',
           13: 'sge', 14: 'exp', 15: 'log', 16: 'lit', 17: 'dst', 18: 'lrp',
           19: 'frc', 20: 'm4x4', 21: 'm4x3', 22: 'm3x4', 23: 'm3x3',
           24: 'm3x2', 25: 'call', 26: 'callnz', 27: 'loop', 28: 'ret',
           29: 'endloop', 30: 'label', 31: 'dcl', 32: 'pow', 33: 'crs',
           34: 'sgn', 35: 'abs', 36: 'nrm', 37: 'sincos', 38: 'rep',
           39: 'endrep', 40: 'if', 41: 'ifc', 42: 'else', 43: 'endif',
           44: 'break', 45: 'breakc', 46: 'mova', 47: 'defb', 48: 'defi',
           64: 'texcoord', 65: 'texkill', 66: 'texld', 67: 'texbem',
           68: 'texbeml', 69: 'texreg2ar', 70: 'texreg2gb', 71: 'texm3x2pad',
           72: 'texm3x2tex', 73: 'texm3x3pad', 74: 'texm3x3tex',
           76: 'texm3x3spec', 77: 'texm3x3vspec', 78: 'expp', 79: 'logp',
           80: 'cnd', 81: 'def', 82: 'texreg2rgb', 83: 'texdp3tex',
           84: 'texm3x2depth', 85: 'texdp3', 86: 'texm3x3', 87: 'texdepth',
           88: 'cmp', 89: 'bem', 90: 'dp2add', 91: 'dsx', 92: 'dsy',
           93: 'texldd', 94: 'setp', 95: 'texldl', 96: 'breakp',
           0xfffd: 'phase'}  # ps_1_4 phase marker: no operands.
FLOW_OPCODES = {'call', 'callnz', 'loop', 'ret', 'endloop', 'label', 'rep',
                'endrep', 'if', 'ifc', 'else', 'endif', 'break', 'breakc',
                'breakp'}
# Destination modifier bits (D3DSP_DSTMOD_SHIFT = 20).
DESTINATION_MODIFIERS = {1: 'saturate', 2: 'partial_precision', 4: 'centroid'}
PREDICATED = 0x10000000
COISSUE = 0x40000000
RELATIVE = 0x00002000

# SM3 stage limits used to report free registers.
VS3_OUTPUTS = 12
PS3_INPUTS = 10
TEMPORARIES = 32

# Reserved ranges the reviewed transformation writes.
VERTEX_MATRIX_CONSTANTS = tuple(range(252, 256))
PIXEL_ABI_CONSTANTS = tuple(range(216, 221))
PIXEL_MOTION_TEMPORARIES = (5, 6, 7)
PIXEL_MOTION_INPUT = 5
PIXEL_MOTION_OUTPUT = 1
VERTEX_MOTION_OUTPUT = 6
MOTION_TEXCOORD_INDEX = 4
# The candidate review bounds the relative light reads to c0-23 by requiring the
# integer count in [0, 8]. That is a runtime check, not a static property.
LIGHT_LOOP_MAX_COUNT = 8
# Emitted classes, in enumerator order. Class C shares the row schema with A
# and B: the transformer validates its branch structure from the words.
HEADER_CLASSES = {'A_reference_registers': 'ReferenceRegisters',
                  'B_relocated_registers': 'RelocatedRegisters',
                  'C_relocated_registers_with_static_branches':
                    'RelocatedRegistersWithBranches'}
# Classes named in the banner but not emitted (none today).
DEFERRED_CLASSES = {}
# Class C admits only `if b#` / `else` / `endif` on boolean constant registers,
# nested at most this deep, balanced, and back at depth 0 before END; the
# transformer asserts the same bound (material_motion.cpp).
STATIC_BRANCH_OPCODES = {'if', 'else', 'endif'}
STATIC_BRANCH_MAX_DEPTH = 1
BOOLEAN_REGISTER_TYPE = 14

# SM2 feasibility (no rows are emitted for SM2; this only quantifies what a
# ps_2_0 / ps_2_x recompilation of the motion fragment would face). Limits are
# the documented profile maxima: ps_2_0 has 64 arithmetic and 32 texture
# instruction slots, 12 temporaries and 32 float constants; ps_2_x (the 2_a
# and 2_b effect directories, both version token 2.1) has up to 512 slots and
# 22 (2_a) or 32 (2_b) temporaries, the actual numbers being device caps
# (D3DPSHADERCAPS2_0); vs_2_0 and vs_2_x have 256 instruction slots and at
# least 256 float constants. The rasterizer clamps oD#/v# colors to [0, 1],
# so only an oT# / t# pair can carry previous clip coordinates, and SM2 links
# oT<n> to t<n> by index.
PS2_LIMITS = {'2_0': {'arithmetic_slots': 64, 'texture_slots': 32, 'temporaries': 12, 'constants': 32},
              '2_a': {'instruction_slots': 512, 'temporaries': 22, 'constants': 32},
              '2_b': {'instruction_slots': 512, 'temporaries': 32, 'constants': 32}}
VS2_INSTRUCTION_SLOTS = 256
SM2_TEXCOORD_LINKS = 8
# Slot costs of the macro instructions; everything else executable costs one.
SLOT_COSTS = {'crs': 2, 'lrp': 2, 'm3x2': 2, 'm3x3': 3, 'm3x4': 4, 'm4x3': 3,
              'm4x4': 4, 'nrm': 3, 'pow': 3, 'sincos': 8}
TEXTURE_OPCODES = {'texld', 'texkill', 'texldd', 'texldl'}
# The authored motion fragment (rigid_motion_pixel_program_inc.h): 25 arithmetic
# instructions, no texture instruction, three DEFs, one input declaration; the
# vertex side adds four DP4s. Its opcodes (mov, add, mad, mul, rcp, dp4, max,
# cmp) exist in ps_2_0.
FRAGMENT_PIXEL_ARITHMETIC = 25
FRAGMENT_PIXEL_TEXTURE = 0
FRAGMENT_PIXEL_TEMPORARIES = 3
FRAGMENT_PIXEL_CONSTANTS = 5
FRAGMENT_VERTEX_INSTRUCTIONS = 4


def register_of(token):
    return ((token >> 28) & 7) | ((token >> 8) & 24), token & 0x7ff


def swizzle_of(token):
    value = (token >> 16) & 0xff
    return ''.join(LANES[(value >> (2 * lane)) & 3] for lane in range(4))


def mask_of(token):
    value = (token >> 16) & 15
    return ''.join(LANES[lane] for lane in range(4) if value & (1 << lane))


def name_of(kind, number):
    if kind == 17:
        return 'vPos' if number == 0 else 'vFace'
    prefix = REGISTER_TYPES.get(kind, 'type%d' % kind)
    return prefix if prefix in ('aL', 'oDepth') else '%s%d' % (prefix, number)


def operand(token, relative_token=None):
    kind, number = register_of(token)
    result = {'register_type': kind, 'register': number,
              'name': name_of(kind, number), 'relative': bool(token & RELATIVE)}
    if relative_token is not None:
        address_kind, address_number = register_of(relative_token)
        result['address_register'] = name_of(address_kind, address_number)
        result['address_component'] = swizzle_of(relative_token)[0]
    return result


class Malformed(ValueError):
    """The candidate is not one complete, walkable SM1-3 program."""


def instructions(code):
    """Walk complete instruction boundaries, keeping comments as opaque data."""
    if len(code) % 4:
        raise Malformed('unaligned_bytecode')
    words = struct.unpack('<%dI' % (len(code) // 4), code)
    if not words:
        raise Malformed('empty_program')
    end = shader_end(words, 0)
    if end is None or end != len(words):
        raise Malformed('not_one_complete_program')
    version = words[0]
    major, minor = (version >> 8) & 255, version & 255
    result, at = [], 1
    while at < len(words):
        token = words[at]
        if token == END:
            if at != len(words) - 1:
                raise Malformed('trailing_data_after_end')
            return words, result, at
        opcode = token & 0xffff
        if opcode == COMMENT:
            at += 1 + ((token >> 16) & 0x7fff)
            continue
        if major == 1:
            length = SM1_ARITY.get(opcode)
            if length is None:
                raise Malformed('unknown_sm1_opcode')
            if minor == 4 and opcode in (64, 66):
                length = 2
        else:
            length = (token >> 24) & 15
        if at + 1 + length > len(words):
            raise Malformed('instruction_exceeds_program')
        result.append({'dword': at, 'token': token, 'opcode': opcode,
                       'length': length, 'words': words[at + 1:at + 1 + length],
                       'predicated': bool(token & PREDICATED),
                       'coissued': bool(token & COISSUE)})
        at += 1 + length
    raise Malformed('missing_end')


def split_operands(item, model_major):
    """Split an instruction's DWORDs into destination and sources.

    SM2+ counts relative-address tokens inside the instruction length, so a
    naive positional read would mistake one for an ordinary operand.
    """
    words = list(item['words'])
    opcode = OPCODES.get(item['opcode'])
    if opcode in ('dcl', 'def', 'defi', 'defb'):
        return None, []
    at, destination = 0, None
    if opcode not in FLOW_OPCODES | {'nop', 'texkill', 'phase'}:
        if not words:
            raise Malformed('missing_destination')
        token = words[0]
        at = 1
        extra = None
        if token & RELATIVE and model_major >= 2:
            if at >= len(words):
                raise Malformed('missing_relative_token')
            extra = words[at]
            at += 1
        destination = operand(token, extra)
        destination['mask'] = mask_of(token)
        modifier = (token >> 20) & 15
        destination['modifiers'] = sorted(name for bit, name in
                                          DESTINATION_MODIFIERS.items() if modifier & bit)
    sources = []
    while at < len(words):
        token = words[at]
        at += 1
        extra = None
        if token & RELATIVE and model_major >= 2:
            if at >= len(words):
                raise Malformed('missing_relative_token')
            extra = words[at]
            at += 1
        item_source = operand(token, extra)
        item_source['swizzle'] = swizzle_of(token)
        sources.append(item_source)
    return destination, sources


def header_boundaries(items):
    """Return (definition_end, header_end, definitions, declarations)."""
    definitions, declarations = [], []
    definition_end = header_end = None
    for item in items:
        opcode = item['opcode']
        if opcode in DEFINITIONS:
            definitions.append({'kind': DEFINITIONS[opcode], 'dword': item['dword'],
                                'dword_count': item['length'] + 1,
                                'end_dword': item['dword'] + item['length'] + 1,
                                **{k: v for k, v in operand(item['words'][0]).items()
                                   if k in ('register_type', 'register', 'name')}})
            definition_end = item['dword'] + item['length'] + 1
            continue
        if opcode == DCL:
            if len(item['words']) != 2:
                raise Malformed('malformed_declaration')
            usage_token, register_token = item['words']
            kind, number = register_of(register_token)
            declaration = {'dword': item['dword'], 'register_type': kind,
                           'register': number, 'name': name_of(kind, number),
                           'mask': mask_of(register_token)}
            modifier = (register_token >> 20) & 15
            declaration['modifiers'] = sorted(name for bit, name in
                                              DESTINATION_MODIFIERS.items() if modifier & bit)
            if kind == 10:
                declaration['role'] = 'sampler'
                declaration['texture_type'] = (usage_token >> 27) & 15
            else:
                # vPos/vFace are MISCTYPE, not ordinary v# interpolators; t# (type
                # 3) are the SM2 pixel texture-coordinate inputs.
                declaration['role'] = ('output' if kind in (4, 5, 6, 8, 9)
                                       else 'misc' if kind == 17
                                       else 'input' if kind in (1, 3) else 'other')
                usage, index = usage_token & 15, (usage_token >> 16) & 15
                declaration['usage'] = usage
                declaration['usage_name'] = USAGES.get(usage, 'usage%d' % usage)
                declaration['usage_index'] = index
            declarations.append(declaration)
            continue
        header_end = item['dword']
        break
    if header_end is None:
        header_end = items[-1]['dword'] + items[-1]['length'] + 1 if items else 1
    if definition_end is None:
        definition_end = declarations[0]['dword'] if declarations else header_end
    return definition_end, header_end, definitions, declarations


def nesting(items):
    """Return (balanced, maximum depth, depth at the program's END)."""
    openers = {'rep', 'loop', 'if', 'ifc'}
    closers = {'endrep': {'rep', 'loop'}, 'endloop': {'rep', 'loop'},
               'endif': {'if', 'ifc'}}
    stack, deepest, balanced = [], 0, True
    for item in items:
        opcode = OPCODES.get(item['opcode'])
        if opcode in openers:
            stack.append(opcode)
            deepest = max(deepest, len(stack))
        elif opcode in closers:
            if not stack or stack[-1] not in closers[opcode]:
                balanced = False
                break
            stack.pop()
        elif opcode == 'else' and (not stack or stack[-1] not in ('if', 'ifc')):
            balanced = False
            break
    return balanced and not stack, deepest, len(stack)


def static_branches(items, decoded):
    """Describe every control-flow instruction: offset, opcode and, for `if`,
    its condition register. `only_boolean_if` is the class C shape: nothing but
    `if`/`else`/`endif`, every condition a direct boolean constant register."""
    sites, only_boolean_if = [], True
    for item, (_, sources) in zip(items, decoded):
        name = OPCODES.get(item['opcode'])
        if name not in FLOW_OPCODES:
            continue
        site = {'dword': item['dword'], 'opcode': name}
        if name == 'if':
            condition = sources[0] if len(sources) == 1 else None
            site['condition'] = condition['name'] if condition else None
            site['condition_register_type'] = condition['register_type'] if condition else None
            if (condition is None or condition['register_type'] != BOOLEAN_REGISTER_TYPE
                    or condition['relative'] or item['predicated']):
                only_boolean_if = False
        elif name not in STATIC_BRANCH_OPCODES or item['predicated'] or item['words']:
            only_boolean_if = False
        sites.append(site)
    return {'sites': sites, 'only_boolean_if': only_boolean_if,
            'boolean_conditions': sorted({site['condition'] for site in sites
                                          if site['opcode'] == 'if' and site.get('condition')})}


def position_site(items, model_major, decoded):
    """Describe the clip-position write; never claim a shape it cannot prove."""
    target = (6, 0) if model_major >= 3 else (4, 0)
    writes = [(item, destination, sources) for item, (destination, sources) in zip(items, decoded)
              if destination and (destination['register_type'], destination['register']) == target]
    if not writes:
        return {'shape': 'absent', 'reason': 'no_position_output_write'}
    dwords = [item['dword'] for item, _, _ in writes]
    result = {'shape': 'unknown', 'write_dwords': dwords,
              'write_count': len(writes),
              'last_write_end_dword': max(item['dword'] + item['length'] + 1
                                          for item, _, _ in writes)}
    lanes, temporaries, rows = {}, set(), {}
    for item, destination, sources in writes:
        if OPCODES.get(item['opcode']) != 'dp4' or len(sources) != 2:
            result['reason'] = 'position_write_is_%s' % (OPCODES.get(item['opcode'], 'unknown'))
            return result
        if item['predicated'] or item['coissued'] or destination['modifiers']:
            result['reason'] = 'position_write_predicated_coissued_or_modified'
            return result
        if len(destination['mask']) != 1:
            result['reason'] = 'position_write_mask_not_single_lane'
            return result
        temporary, row = sources
        if temporary['register_type'] != 0 or temporary['relative'] or temporary['swizzle'] != 'xyzw':
            result['reason'] = 'position_dot_source_not_plain_temporary'
            return result
        if row['register_type'] not in CONSTANT_TYPES or row['relative'] or row['swizzle'] != 'xyzw':
            result['reason'] = 'position_dot_row_not_direct_plain_constant'
            return result
        lane = destination['mask']
        if lane in lanes:
            result['reason'] = 'duplicate_position_lane'
            return result
        lanes[lane] = item['dword']
        temporaries.add(temporary['register'])
        rows[lane] = row['register']
    if set(lanes) != set(LANES):
        result['reason'] = 'position_lanes_incomplete'
        return result
    if len(temporaries) != 1:
        result['reason'] = 'different_position_source_temporaries'
        return result
    base = rows['x']
    ordered = [lanes[lane] for lane in LANES]
    lengths = {item['dword']: item['length'] + 1 for item, _, _ in writes}
    contiguous = all(ordered[i] + lengths[ordered[i]] == ordered[i + 1] for i in range(3))
    issue_order = ''.join(sorted(LANES, key=lambda lane: lanes[lane]))
    result.update({'shape': 'row_dot_quad', 'reason': None,
                   'source_temporary': sorted(temporaries)[0],
                   'matrix_register': base,
                   'rows_xyzw': [rows[lane] for lane in LANES],
                   'rows_consecutive': [rows[lane] for lane in LANES] == list(range(base, base + 4)),
                   'dwords_xyzw': ordered,
                   'issue_order': issue_order,
                   'contiguous_quad': contiguous,
                   'quad_first_dword': min(ordered),
                   'insertion_dword': max(item['dword'] + item['length'] + 1
                                          for item, _, _ in writes),
                   'instruction_count': len(writes)})
    result['operands_xyzw'] = [
        {'opcode': 'dp4', 'destination': {'name': 'o0' if model_major >= 3 else 'oPos',
                                          'register_type': target[0], 'register': target[1],
                                          'mask': lane},
         'sources': [{'name': 'r%d' % result['source_temporary'], 'swizzle': 'xyzw'},
                     {'name': 'c%d' % rows[lane], 'swizzle': 'xyzw'}]}
        for lane in LANES]
    return result


def profile(code, identifier, stage, model):
    digest = {'id': identifier, 'stage': stage, 'model': model,
              'bytes': len(code), 'fnv1a64': fnv1a64(code),
              'sha256': hashlib.sha256(code).hexdigest()}
    try:
        words, items, end_dword = instructions(code)
    except Malformed as error:
        return {**digest, 'parsed': False, 'reason': str(error)}
    major = (words[0] >> 8) & 255
    digest.update({'parsed': True, 'version': '0x%08x' % words[0],
                   'dword_count': len(words), 'end_dword': end_dword,
                   'instruction_count': len(items),
                   'comment_dword_count': len(words) - 1 - sum(i['length'] + 1 for i in items)})
    decoded = []
    for item in items:
        try:
            decoded.append(split_operands(item, major))
        except Malformed as error:
            return {**digest, 'parsed': False, 'reason': str(error)}
    definition_end, header_end, definitions, declarations = header_boundaries(items)
    digest.update({'definition_end_dword': definition_end, 'header_end_dword': header_end,
                   'literal_definitions': definitions, 'declarations': declarations,
                   'declaration_count': len(declarations)})
    out_of_header = [item['dword'] for item in items
                     if item['dword'] >= header_end and item['opcode'] in HEADER_OPCODES]
    digest['header_is_contiguous'] = not out_of_header
    digest['late_header_instruction_dwords'] = out_of_header

    opcode_counts = Counter(OPCODES.get(item['opcode'], 'opcode%d' % item['opcode']) for item in items)
    digest['opcode_counts'] = dict(sorted(opcode_counts.items()))
    digest['control_flow_counts'] = {name: opcode_counts[name]
                                     for name in sorted(FLOW_OPCODES) if opcode_counts[name]}
    digest['predicated_or_coissued_dwords'] = [item['dword'] for item in items
                                               if item['predicated'] or item['coissued']]
    digest['texkill_dwords'] = [item['dword'] for item in items
                               if OPCODES.get(item['opcode']) == 'texkill']
    digest['executable_instruction_count'] = sum(1 for item in items
                                                 if item['opcode'] not in HEADER_OPCODES)
    balanced, deepest, residual = nesting(items)
    digest['control_flow_balanced'] = balanced
    digest['control_flow_max_depth'] = deepest
    digest['control_flow_depth_at_end'] = residual
    digest['static_branches'] = static_branches(items, decoded)

    read, written, relative_sites = Counter(), Counter(), []
    defined_registers = {(item['register_type'], item['register']) for item in definitions}
    for item, (destination, sources) in zip(items, decoded):
        if item['opcode'] in HEADER_OPCODES:
            continue
        if destination:
            written[(destination['register_type'], destination['register'])] += 1
        for source in sources:
            read[(source['register_type'], source['register'])] += 1
            if source['relative']:
                relative_sites.append({'dword': item['dword'],
                                       'opcode': OPCODES.get(item['opcode']),
                                       'base_register_type': source['register_type'],
                                       'base_register': source['register'],
                                       'address_register': source.get('address_register'),
                                       'address_component': source.get('address_component')})
        if destination and destination['relative']:
            relative_sites.append({'dword': item['dword'],
                                   'opcode': OPCODES.get(item['opcode']),
                                   'base_register_type': destination['register_type'],
                                   'base_register': destination['register'],
                                   'address_register': destination.get('address_register'),
                                   'address_component': destination.get('address_component'),
                                   'destination': True})
    referenced = set(read) | set(written)

    def numbers(kinds, source=referenced):
        return sorted({number for kind, number in source if kind in kinds})

    def prefix(kind):
        # Stage-aware spelling: SM2 vertex programs write oT#/oD#/oPos-class
        # registers where SM3 declares o#, and SM2 pixel programs read t#.
        if stage == 'vs' and major < 3 and kind == 6:
            return 'oT'
        if stage == 'vs' and kind == 5:
            return 'oD'
        if stage == 'ps' and kind == 3:
            return 't'
        return REGISTER_TYPES.get(kind, 'type%d' % kind)

    def by_type(source):
        result = {}
        for kind, number in source:
            result.setdefault(prefix(kind), []).append(number)
        return {name: sorted(set(values)) for name, values in sorted(result.items())}

    digest['registers_referenced'] = {'read': by_type(read), 'written': by_type(written)}

    constants_direct = numbers(CONSTANT_TYPES)
    digest['constant_registers_direct'] = constants_direct
    digest['highest_direct_constant'] = max(constants_direct, default=None)
    digest['defined_constant_registers'] = sorted(number for kind, number in defined_registers
                                                  if kind in CONSTANT_TYPES)
    digest['integer_registers'] = numbers({7})
    digest['boolean_registers'] = numbers({14})
    digest['temporary_registers'] = numbers({0})
    digest['highest_temporary'] = max(digest['temporary_registers'], default=None)
    digest['sampler_registers'] = sorted({d['register'] for d in declarations
                                          if d.get('role') == 'sampler'})
    digest['relative_addressing'] = {
        'present': bool(relative_sites),
        'sites': relative_sites,
        'address_registers': sorted({site['address_register'] for site in relative_sites
                                     if site.get('address_register')}),
        'mova_count': opcode_counts['mova'],
        'rep_count': opcode_counts['rep'],
        'loop_count': opcode_counts['loop'],
        'loop_bound_integer_registers': numbers({7}),
        'bounded_by_static_analysis': False,
        'note': 'A base register is a syntactic reference only; the reachable '
                'constant range depends on the runtime address value. Bound it '
                'with the runtime integer/loop check, not this table.'}

    digest['centroid_declarations'] = [d['dword'] for d in declarations if 'centroid' in d['modifiers']]
    digest['partial_precision_declarations'] = [d['dword'] for d in declarations
                                                if 'partial_precision' in d['modifiers']]

    if stage == 'vs':
        outputs = {d['register'] for d in declarations if d['role'] == 'output' and d['register_type'] == 6}
        written_outputs = numbers({6}, set(written))
        used = outputs | set(written_outputs)
        digest['declared_output_registers'] = sorted(outputs)
        digest['written_output_registers'] = written_outputs
        digest['free_output_registers'] = ([n for n in range(VS3_OUTPUTS) if n not in used]
                                           if major >= 3 else [])
        # SM2: fixed oT0-7 texture-coordinate and oD0-1 color outputs.
        digest['free_texcoord_outputs'] = ([n for n in range(SM2_TEXCOORD_LINKS) if n not in used]
                                           if major == 2 else [])
        digest['free_color_outputs'] = ([n for n in range(2) if n not in set(numbers({5}, set(written)))]
                                        if major == 2 else [])
        digest['declared_inputs'] = [{'register': d['register'], 'usage': d['usage'],
                                      'usage_index': d['usage_index'], 'usage_name': d['usage_name'],
                                      'mask': d['mask'], 'dword': d['dword']}
                                     for d in declarations if d['role'] == 'input']
        digest['declared_outputs'] = [{'register': d['register'], 'register_type': d['register_type'],
                                       'usage': d['usage'], 'usage_index': d['usage_index'],
                                       'usage_name': d['usage_name'], 'mask': d['mask'],
                                       'modifiers': d['modifiers'], 'dword': d['dword']}
                                      for d in declarations if d['role'] == 'output']
        used_texcoord = sorted({d['usage_index'] for d in declarations
                                if d['role'] == 'output' and d.get('usage') == 5})
        digest['declared_texcoord_output_indices'] = used_texcoord
        digest['position_output'] = position_site(items, major, decoded)
        site = digest['position_output']
        digest['matrix_register'] = site.get('matrix_register')
        digest['insertion_dword'] = site.get('insertion_dword')
        digest['reserved_constants_free'] = all(number not in constants_direct
                                                for number in VERTEX_MATRIX_CONSTANTS)
        digest['reserved_constants'] = list(VERTEX_MATRIX_CONSTANTS)
        digest['vertex_texture_fetch_dwords'] = [item['dword'] for item in items
                                                 if OPCODES.get(item['opcode'], '').startswith('tex')]
        if site.get('shape') == 'row_dot_quad':
            temporary = site['source_temporary']
            last = site['insertion_dword']
            rewrites = [item['dword'] for item, (destination, _) in zip(items, decoded)
                        if destination and destination['register_type'] == 0
                        and destination['register'] == temporary
                        and site['quad_first_dword'] < item['dword'] < last]
            digest['position_temporary_rewritten_inside_quad'] = rewrites
            # Control-flow instructions between the first dot and the insert:
            # a non-adjacent quad may only span straight-line code.
            digest['position_flow_inside_quad'] = [
                item['dword'] for item in items
                if OPCODES.get(item['opcode']) in FLOW_OPCODES
                and site['quad_first_dword'] < item['dword'] < last]
    else:
        digest['declared_inputs'] = [{'register': d['register'], 'register_type': d['register_type'],
                                      'usage': d['usage'],
                                      'usage_index': d['usage_index'], 'usage_name': d['usage_name'],
                                      'mask': d['mask'], 'modifiers': d['modifiers'],
                                      'dword': d['dword']}
                                     for d in declarations if d['role'] == 'input']
        digest['declared_samplers'] = [{'register': d['register'], 'texture_type': d['texture_type'],
                                        'dword': d['dword']} for d in declarations
                                       if d['role'] == 'sampler']
        digest['declared_misc_registers'] = [d['name'] for d in declarations if d['role'] == 'misc']
        digest['declared_texcoord_input_indices'] = sorted({d['usage_index'] for d in declarations
                                                            if d['role'] == 'input' and d['usage'] == 5})
        used_inputs = {d['register'] for d in digest['declared_inputs']
                       if d['register_type'] == 1} | set(numbers({1}, set(read)))
        digest['free_input_registers'] = ([n for n in range(PS3_INPUTS) if n not in used_inputs]
                                          if major >= 3 else [])
        # SM2: t0-7 texture-coordinate inputs, declared or read.
        used_texture_inputs = {d['register'] for d in digest['declared_inputs']
                               if d['register_type'] == 3} | set(numbers({3}))
        digest['free_texture_inputs'] = ([n for n in range(SM2_TEXCOORD_LINKS) if n not in used_texture_inputs]
                                         if major == 2 else [])
        digest['color_outputs'] = numbers({8}, set(written))
        digest['depth_output_dwords'] = [item['dword'] for item, (destination, _) in zip(items, decoded)
                                         if destination and destination['register_type'] == 9]
        digest['free_temporaries'] = ([n for n in range(TEMPORARIES)
                                       if n not in set(digest['temporary_registers'])]
                                      if major >= 2 else [])
        digest['reserved_constants'] = list(PIXEL_ABI_CONSTANTS)
        digest['reserved_constants_free'] = all(number not in constants_direct
                                                for number in PIXEL_ABI_CONSTANTS)
        digest['reserved_temporaries'] = list(PIXEL_MOTION_TEMPORARIES)
        digest['reserved_temporaries_free'] = all(number not in digest['temporary_registers']
                                                  for number in PIXEL_MOTION_TEMPORARIES)
        digest['reserved_input_free'] = PIXEL_MOTION_INPUT not in used_inputs
        digest['reserved_output_free'] = PIXEL_MOTION_OUTPUT not in digest['color_outputs']
        digest['append_dword'] = end_dword
    return digest


def classify(vertex, pixel):
    """Group a pair by what a table-driven transformer would have to change.

    The reference plan is the reviewed Argon splice: VS o6/TEXCOORD4 fed by four
    DP4s against c252-255, PS v5 plus r5-7, c216-220 and oC1. A program that
    only needs different *indices* for those roles stays in the same offset-table
    family; a program whose position or program shape differs does not.
    """
    blocking, differences, plan = [], [], {}
    if not vertex.get('parsed') or not pixel.get('parsed'):
        return 'X_unparsed', ['program_not_walkable'], [], plan
    if vertex['model'] != '3_0' or pixel['model'] != '3_0':
        blocking.append('not_sm3:vs_%s/ps_%s' % (vertex['model'], pixel['model']))
    if vertex.get('predicated_or_coissued_dwords') or pixel.get('predicated_or_coissued_dwords'):
        blocking.append('predicated_or_coissued_instruction')
    if not vertex.get('header_is_contiguous') or not pixel.get('header_is_contiguous'):
        blocking.append('header_not_contiguous')
    site = vertex.get('position_output', {})
    if site.get('shape') != 'row_dot_quad':
        blocking.append('vs_position_%s:%s' % (site.get('shape'), site.get('reason')))
    else:
        if not site['rows_consecutive']:
            blocking.append('vs_position_rows_not_consecutive')
        if site['issue_order'] != 'xyzw':
            blocking.append('vs_position_issue_order_%s' % site['issue_order'])
        # The four dots need not be adjacent: the transformer inserts after the
        # last one and revalidates that the span rewrites no position temporary
        # and crosses no control-flow instruction (the asteroid, moon and
        # planet_haze light-free variants interleave other work between them).
        if vertex.get('position_temporary_rewritten_inside_quad'):
            blocking.append('vs_position_temporary_rewritten_inside_quad')
        if vertex.get('position_flow_inside_quad'):
            blocking.append('vs_position_block_boundary_inside_quad')
    if vertex.get('vertex_texture_fetch_dwords'):
        blocking.append('vs_vertex_texture_fetch')
    if pixel.get('texkill_dwords'):
        blocking.append('ps_texkill')
    if pixel.get('depth_output_dwords'):
        blocking.append('ps_depth_output')
    if pixel.get('color_outputs') != [0]:
        blocking.append('ps_color_outputs_%s' % pixel.get('color_outputs'))
    if not pixel.get('control_flow_balanced') or pixel.get('control_flow_depth_at_end'):
        blocking.append('ps_control_flow_not_balanced')
    elif pixel.get('control_flow_counts'):
        # Class C: static boolean branches only, nested at most one deep.
        if not (pixel.get('static_branches') or {}).get('only_boolean_if'):
            blocking.append('ps_control_flow_not_static_boolean_if')
        if pixel.get('control_flow_max_depth', 0) > STATIC_BRANCH_MAX_DEPTH:
            blocking.append('ps_control_flow_depth_%d_exceeds_%d' % (
                pixel['control_flow_max_depth'], STATIC_BRANCH_MAX_DEPTH))
    if not vertex.get('reserved_constants_free'):
        blocking.append('vs_constants_c252_255_used')
    if not pixel.get('reserved_constants_free'):
        blocking.append('ps_constants_c216_220_used')
    if not pixel.get('reserved_output_free'):
        blocking.append('ps_output_oC1_used')

    outputs = vertex.get('free_output_registers') or []
    inputs = pixel.get('free_input_registers') or []
    temporaries = pixel.get('free_temporaries') or []
    texcoords = sorted(set(range(16)) - set(vertex.get('declared_texcoord_output_indices') or [])
                       - set(pixel.get('declared_texcoord_input_indices') or []))
    if not outputs:
        blocking.append('vs_no_free_output_register')
    if not inputs:
        blocking.append('ps_no_free_input_register')
    if len(temporaries) < 3:
        blocking.append('ps_fewer_than_three_free_temporaries')
    if not texcoords:
        blocking.append('no_free_texcoord_index')
    if blocking:
        return ('X_not_sm3_material' if any(r.startswith('not_sm3') for r in blocking)
                else 'X_position_not_row_dot' if any(r.startswith('vs_position') for r in blocking)
                else 'X_unsupported'), blocking, differences, plan

    # Prefer the reviewed Argon choices wherever they are still free, so a
    # "difference" always means the reference register is genuinely occupied.
    available = set(temporaries)
    runs = [n for n in temporaries if {n, n + 1, n + 2} <= available]
    if not runs:
        blocking.append('ps_no_three_consecutive_free_temporaries')
        return 'X_unsupported', blocking, differences, plan
    base = (PIXEL_MOTION_TEMPORARIES[0] if PIXEL_MOTION_TEMPORARIES[0] in runs
            else runs[0])
    plan = {'vs_output_register': (VERTEX_MOTION_OUTPUT if VERTEX_MOTION_OUTPUT in outputs
                                   else outputs[0]),
            'ps_input_register': (PIXEL_MOTION_INPUT if PIXEL_MOTION_INPUT in inputs
                                  else inputs[0]),
            'ps_temporary_base': base,
            'ps_temporaries': [base, base + 1, base + 2],
            'texcoord_index': (MOTION_TEXCOORD_INDEX if MOTION_TEXCOORD_INDEX in texcoords
                               else texcoords[0]),
            'vs_declaration_insert_dword': vertex['header_end_dword'],
            'vs_arithmetic_insert_dword': vertex['insertion_dword'],
            'vs_matrix_register': vertex['matrix_register'],
            'vs_position_temporary': site['source_temporary'],
            'vs_position_dp4_dwords': list(site['dwords_xyzw']),
            'vs_position_lane_masks': [1, 2, 4, 8],
            'vs_position_quad_contiguous': site['contiguous_quad'],
            'vs_constant_base': VERTEX_MATRIX_CONSTANTS[0],
            'ps_constant_base': PIXEL_ABI_CONSTANTS[0],
            'ps_output_register': PIXEL_MOTION_OUTPUT,
            'ps_definition_insert_dword': pixel['definition_end_dword'],
            'ps_declaration_insert_dword': pixel['header_end_dword'],
            'ps_append_dword': pixel['append_dword'],
            'light_loop_bound_required': vertex['relative_addressing']['present'],
            'light_loop_max_count': LIGHT_LOOP_MAX_COUNT}
    if plan['vs_output_register'] != VERTEX_MOTION_OUTPUT:
        differences.append('vs_output_register_%d' % plan['vs_output_register'])
    if plan['ps_input_register'] != PIXEL_MOTION_INPUT:
        differences.append('ps_input_register_v%d' % plan['ps_input_register'])
    if tuple(plan['ps_temporaries']) != PIXEL_MOTION_TEMPORARIES:
        differences.append('ps_temporaries_%s' % ','.join('r%d' % r for r in plan['ps_temporaries']))
    if plan['texcoord_index'] != MOTION_TEXCOORD_INDEX:
        differences.append('texcoord_index_%d' % plan['texcoord_index'])
    if pixel.get('control_flow_counts'):
        differences.append('ps_control_flow_%s' % ','.join(sorted(pixel['control_flow_counts'])))
    if pixel.get('declared_misc_registers'):
        differences.append('ps_misc_inputs_%s' % ','.join(pixel['declared_misc_registers']))
    if not site['contiguous_quad']:
        differences.append('vs_position_quad_not_contiguous')

    flow = bool(pixel.get('control_flow_counts'))
    relocated = any(d.startswith(('vs_output_register', 'ps_input_register',
                                  'ps_temporaries', 'texcoord_index')) for d in differences)
    if not relocated and not flow:
        name = 'A_reference_registers'
    elif not flow:
        name = 'B_relocated_registers'
    else:
        name = 'C_relocated_registers_with_static_branches'
    return name, blocking, differences, plan


HEADER_FIELDS = (
    'vertex_fingerprint', 'vertex_dword_count', 'vertex_version',
    'pixel_fingerprint', 'pixel_dword_count', 'pixel_version',
    'transformation_class',
    'matrix_register', 'position_temporary',
    'position_dp4_dwords[4]', 'position_lane_masks[4]',
    'vertex_declaration_insert_dword', 'vertex_arithmetic_insert_dword',
    'pixel_definition_insert_dword', 'pixel_declaration_insert_dword',
    'pixel_append_dword',
    'vertex_output_register', 'texcoord_index',
    'pixel_input_register', 'pixel_temporary_base', 'pixel_output_register',
    'vertex_constant_base', 'pixel_constant_base',
    'light_loop_bound_required', 'light_loop_max_count',
    'vertex_depth_output_register', 'depth_texcoord_index',
    'pixel_depth_input_register', 'depth_output',
    'observed_scene_draws')
# Row fields the header carries beyond the insertion plan: the current-depth
# interpolator (see depth_plan). NONE marks a register the program cannot spare.
DEPTH_NONE = 255


def header_rows(result):
    """Order the emitted rows deterministically and independently of input order."""
    rows = [pair for pair in result['pairs']
            if pair['transformation_class'] in HEADER_CLASSES]
    return sorted(rows, key=lambda pair: (-pair['observed_draws'], pair['vs'], pair['ps']))


def depth_plan(result):
    """Second interpolator per row: the current clip z/w the pixel variant
    divides into RT2 (R32F device depth).

    The live route creates one variant per original program, so the choice
    must be consistent across every row a program takes part in: the vertex
    register is the first free output other than the motion output (the same
    for every row of a VS, because rows sharing a VS agree on the motion
    output); the pixel register is the first free input other than the motion
    input; the TEXCOORD index is chosen per connected component of the
    VS/PS sharing graph as the smallest index declared by no program of the
    component and different from its motion index. A component whose VS side
    cannot export (no register or no index) gets no depth output at all; a PS
    without a spare input keeps motion only (its VS may still export an
    interpolator nobody reads, which SM3 permits). Rows sharing a PS therefore
    agree on `depth_output`, which the per-program pixel variant relies on.
    """
    programs = result['programs']
    rows = header_rows(result)
    parent = {}

    def find(node):
        while parent.setdefault(node, node) != node:
            parent[node] = parent[parent[node]]
            node = parent[node]
        return node

    for pair in rows:
        parent[find('vs_' + pair['vs'])] = find('ps_' + pair['ps'])
    components = {}
    for pair in rows:
        components.setdefault(find('vs_' + pair['vs']), set()).update({'vs_' + pair['vs'], 'ps_' + pair['ps']})
    vertex_register, pixel_register, motion_index = {}, {}, {}
    for pair in rows:
        plan = pair['insertion_plan']
        vertex = programs['vs_' + pair['vs']]
        pixel = programs['ps_' + pair['ps']]
        free = [n for n in vertex.get('free_output_registers') or [] if n != plan['vs_output_register']]
        vertex_register.setdefault(pair['vs'], free[0] if free else DEPTH_NONE)
        free = [n for n in pixel.get('free_input_registers') or [] if n != plan['ps_input_register']]
        pixel_register.setdefault(pair['ps'], free[0] if free else DEPTH_NONE)
        motion_index.setdefault(find('vs_' + pair['vs']), set()).add(plan['texcoord_index'])
    texcoord = {}
    exportable = {}
    for root, members in components.items():
        used = set(motion_index[root])
        for name in members:
            program = programs[name]
            used.update(program.get('declared_texcoord_output_indices') or [])
            used.update(program.get('declared_texcoord_input_indices') or [])
        free = [n for n in range(16) if n not in used]
        texcoord[root] = free[0] if free else DEPTH_NONE
        exportable[root] = texcoord[root] != DEPTH_NONE and all(
            vertex_register[name[3:]] != DEPTH_NONE for name in members if name.startswith('vs_'))
    result_rows = {}
    for pair in rows:
        root = find('vs_' + pair['vs'])
        exports = exportable[root]
        result_rows[(pair['vs'], pair['ps'])] = {
            'vertex_depth_output_register': vertex_register[pair['vs']] if exports else DEPTH_NONE,
            'depth_texcoord_index': texcoord[root] if exports else DEPTH_NONE,
            'pixel_depth_input_register': pixel_register[pair['ps']],
            'depth_output': exports and pixel_register[pair['ps']] != DEPTH_NONE}
    return result_rows


def render_header(result):
    """Render the constexpr row list; derived numbers and version tokens only."""
    programs = result['programs']
    reference = '%s + %s' % (REFERENCE['vs'][0], REFERENCE['ps'][0])
    passes = result['pass_table']
    lines = [
        '// Generated by tools/analysis/inspect_motion_output_profiles.py; derived metadata only.',
        '// Reviewed full shader sweep SHA256:',
        '//   ' + result['inventory_sha256'],
        '// Reference pair, see docs/reverse-engineering/motion-output-candidate.md:',
        '//   ' + reference,
        '// Rows are every transformable SM3 pair bound by a technique pass of the',
        '// %d installed compiled effects (%d passes, %d SM3 pairs), ordered by' % (
            passes['effect_count'], passes['pass_count'], result['pair_count']),
        '// descending captured Scene draws (metadata: one session, zero when the',
        '// capture never drew the pair) then by vertex and pixel fingerprint.',
        '// Classes emitted:',
    ]
    lines += ['//   %s = MotionOutputClass::%s' % item for item in sorted(HEADER_CLASSES.items())]
    if DEFERRED_CLASSES:
        lines.append('// Deferred, name reserved so the schema does not change:')
        lines += ['//   %s = MotionOutputClass::%s' % item for item in sorted(DEFERRED_CLASSES.items())]
    lines += [
        '// RelocatedRegistersWithBranches rows: the pixel program holds only',
        '// if b#/else/endif blocks (boolean constant conditions, nesting depth <= %d,' % STATIC_BRANCH_MAX_DEPTH,
        '// balanced, depth 0 at the append point); the transformer revalidates this.',
        '// position_dp4_dwords need not be adjacent: the arithmetic insert follows the',
        '// last dot and the span between the first dot and the insert is revalidated',
        '// to rewrite no position temporary and hold no control-flow instruction.',
        '// vertex_depth_output_register / depth_texcoord_index / pixel_depth_input_register',
        '// carry the current clip z/w interpolator for the R32F depth target (RT2);',
        '// %d means none. depth_output=false keeps the row motion-only. The index' % DEPTH_NONE,
        '// is shared by every program of one VS/PS sharing component (see depth_plan).',
    ]
    lines.append('// Field order:')
    for index in range(0, len(HEADER_FIELDS), 3):
        lines.append('//   ' + ', '.join(HEADER_FIELDS[index:index + 3]))
    lines += [
        '// DWORD offsets are zero-based from the version token and include comments.',
        '// Offsets and registers are inputs to a transformation that must still',
        '// revalidate the program it is given; they are not an eligibility decision.',
        '// light_loop_bound_required means the vertex program reads constants',
        '// relatively: refuse the variant unless integer i0.x is checked in',
        '// [0, light_loop_max_count] at draw time.',
    ]
    depth = depth_plan(result)
    for pair in header_rows(result):
        plan = pair['insertion_plan']
        vertex = programs['vs_' + pair['vs']]
        pixel = programs['ps_' + pair['ps']]
        depth_row = depth[(pair['vs'], pair['ps'])]
        lines += [
            '{0x%sull, %d, %su,' % (pair['vs'], vertex['dword_count'], vertex['version']),
            ' 0x%sull, %d, %su,' % (pair['ps'], pixel['dword_count'], pixel['version']),
            ' MotionOutputClass::%s, %d, %d, {%s}, {%s},' % (
                HEADER_CLASSES[pair['transformation_class']],
                plan['vs_matrix_register'], plan['vs_position_temporary'],
                ', '.join(str(value) for value in plan['vs_position_dp4_dwords']),
                ', '.join(str(value) for value in plan['vs_position_lane_masks'])),
            ' %d, %d, %d, %d, %d,' % (
                plan['vs_declaration_insert_dword'], plan['vs_arithmetic_insert_dword'],
                plan['ps_definition_insert_dword'], plan['ps_declaration_insert_dword'],
                plan['ps_append_dword']),
            ' %d, %d, %d, %d, %d, %d, %d, %s, %d,' % (
                plan['vs_output_register'], plan['texcoord_index'],
                plan['ps_input_register'], plan['ps_temporary_base'],
                plan['ps_output_register'], plan['vs_constant_base'],
                plan['ps_constant_base'],
                'true' if plan['light_loop_bound_required'] else 'false',
                plan['light_loop_max_count']),
            ' %d, %d, %d, %s, %d},' % (
                depth_row['vertex_depth_output_register'], depth_row['depth_texcoord_index'],
                depth_row['pixel_depth_input_register'],
                'true' if depth_row['depth_output'] else 'false', pair['observed_draws']),
        ]
    return '\n'.join(lines) + '\n'


DRAW = re.compile(rb'^draw .*? vs=([0-9a-f]{16}) ps=([0-9a-f]{16})')
CLEAR = re.compile(rb'^clear flags=(\d+)')


def capture_pairs(path):
    """Count captured draws per VS/PS pair, split by in-frame clear segment.

    The capture has no per-draw phase field. Segment 1 is the span between the
    first depth-only Clear of a frame and the next Clear; in this capture that
    is the material span the selector calls Scene. It is a derived approximation
    of that phase, not the selector's own decision.
    """
    frames, segment = set(), 0
    counts, segments = Counter(), Counter()
    digest = hashlib.sha256()
    size = 0
    with path.open('rb') as stream:
        for line in stream:
            digest.update(line)
            size += len(line)
            if line.startswith(b'scene_depth_frame phase=begin'):
                segment = 0
                continue
            clear = CLEAR.match(line)
            if clear:
                if int(clear[1]) == 2:
                    segment += 1
                continue
            draw = DRAW.match(line)
            if draw:
                key = (draw[1].decode(), draw[2].decode())
                counts[(key, segment)] += 1
                segments[segment] += 1
                frames.add(line.split(b' frame=', 1)[1].split(b' ', 1)[0])
    return {'source_basename': path.name, 'source_bytes': size,
            'source_sha256': digest.hexdigest(), 'frame_count': len(frames),
            'draw_count': sum(segments.values()),
            'draws_per_clear_segment': dict(sorted(segments.items())),
            'scene_segment': 1,
            'scene_draw_count': segments[1],
            'pairs': [{'vs': key[0], 'ps': key[1], 'clear_segment': segment, 'draws': count}
                      for (key, segment), count in counts.most_common()],
            'scope': 'One captured session; clear-segment phase approximation, '
                     'not the runtime selector decision or full-frame coverage.'}


def instruction_slots(program):
    """(arithmetic, texture) instruction slots of a parsed program's executable
    instructions, macro instructions at their documented cost."""
    arithmetic = texture = 0
    for name, count in (program.get('opcode_counts') or {}).items():
        if name in ('dcl', 'def', 'defb', 'defi'):
            continue
        if name in TEXTURE_OPCODES:
            texture += count
        else:
            arithmetic += count * SLOT_COSTS.get(name, 1)
    return arithmetic, texture


def consecutive_run(free, length):
    """First base of `length` consecutive free registers, or None."""
    available = set(free)
    for base in sorted(free):
        if all(base + offset in available for offset in range(length)):
            return base
    return None


def pixel_profile(pixel, directories):
    """ps_2_0 by token; a 2.1 token is ps_2_x, whose limits follow the effect
    directory (2_a is the tighter of the two when both host the program)."""
    if pixel['model'] == '2_0':
        return '2_0'
    return '2_a' if '2_a' in directories or '2_b' not in directories else '2_b'


SM1_REASON = 'ps_1_x has no second color target (oC1) and no declared o#/v# linkage'
SM2_GROUPS = ('hostable_with_ps_2_0_fragment', 'hostable_with_ps_2_x_fragment',
              'exceeds_limits', 'structurally_unsupported')


def sm2_feasibility(vertex, pixel, directories):
    """What a ps_2_0 / ps_2_x recompilation of the motion fragment would face.

    No SM2 row is emitted; this quantifies the remainder. The structural rules
    mirror the SM3 classifier (row-dot position quad, free carrier, free
    output, no texkill/oDepth/predication, static boolean branches only) with
    the SM2 linkage: previous clip must travel oT<n> -> t<n> on one free index
    in both programs, and the fragment's registers and instructions must fit
    the profile's documented limits.
    """
    if not vertex.get('parsed') or not pixel.get('parsed'):
        return {'group': 'structurally_unsupported', 'reasons': ['program_not_walkable'],
                'exceeded': {}, 'pixel_profile': None, 'position': {}, 'vertex': {}, 'pixel': {}}
    name = pixel_profile(pixel, directories)
    limits = PS2_LIMITS[name]
    structural, exceeded = [], {}
    site = vertex.get('position_output') or {}
    position = {key: site.get(key) for key in (
        'shape', 'reason', 'matrix_register', 'rows_xyzw', 'rows_consecutive',
        'contiguous_quad', 'issue_order', 'source_temporary', 'insertion_dword')}
    if site.get('shape') != 'row_dot_quad':
        structural.append('vs_position_%s:%s' % (site.get('shape'), site.get('reason')))
    else:
        if not site['rows_consecutive']:
            structural.append('vs_position_rows_not_consecutive')
        if site['issue_order'] != 'xyzw':
            structural.append('vs_position_issue_order_%s' % site['issue_order'])
        # Same span rule as the SM3 classifier: dots need not be adjacent.
        if vertex.get('position_temporary_rewritten_inside_quad'):
            structural.append('vs_position_temporary_rewritten_inside_quad')
        if vertex.get('position_flow_inside_quad'):
            structural.append('vs_position_block_boundary_inside_quad')
    if vertex['predicated_or_coissued_dwords'] or pixel['predicated_or_coissued_dwords']:
        structural.append('predicated_or_coissued_instruction')
    if not vertex['header_is_contiguous'] or not pixel['header_is_contiguous']:
        structural.append('header_not_contiguous')
    if not vertex['reserved_constants_free']:
        structural.append('vs_constants_c252_255_used')
    links = sorted(set(vertex['free_texcoord_outputs']) & set(pixel['free_texture_inputs']))
    if not links:
        structural.append('no_free_oT_t_link_index')
    if pixel['texkill_dwords']:
        structural.append('ps_texkill')
    if pixel['depth_output_dwords']:
        structural.append('ps_depth_output')
    if pixel['color_outputs'] != [0]:
        structural.append('ps_color_outputs_%s' % pixel['color_outputs'])
    if not pixel['reserved_output_free']:
        structural.append('ps_output_oC1_used')
    if pixel['relative_addressing']['present']:
        structural.append('ps_relative_addressing')
    if not pixel['control_flow_balanced'] or pixel['control_flow_depth_at_end']:
        structural.append('ps_control_flow_not_balanced')
    elif pixel['control_flow_counts']:
        if not pixel['static_branches']['only_boolean_if']:
            structural.append('ps_control_flow_not_static_boolean_if')
        if pixel['control_flow_max_depth'] > STATIC_BRANCH_MAX_DEPTH:
            structural.append('ps_control_flow_depth_%d_exceeds_%d' % (
                pixel['control_flow_max_depth'], STATIC_BRANCH_MAX_DEPTH))
    arithmetic, texture = instruction_slots(pixel)
    vertex_slots = sum(instruction_slots(vertex))
    free_temporaries = [n for n in range(limits['temporaries']) if n not in pixel['temporary_registers']]
    used_constants = set(pixel['constant_registers_direct']) | set(pixel['defined_constant_registers'])
    free_constants = [n for n in range(limits['constants']) if n not in used_constants]
    if name == '2_0':
        budget = {'arithmetic_slots': arithmetic + FRAGMENT_PIXEL_ARITHMETIC,
                  'texture_slots': texture + FRAGMENT_PIXEL_TEXTURE}
    else:
        budget = {'instruction_slots': arithmetic + texture + FRAGMENT_PIXEL_ARITHMETIC + FRAGMENT_PIXEL_TEXTURE}
    for key, value in budget.items():
        if value > limits[key]:
            exceeded[key] = value - limits[key]
    if len(free_temporaries) < FRAGMENT_PIXEL_TEMPORARIES:
        exceeded['temporaries'] = FRAGMENT_PIXEL_TEMPORARIES - len(free_temporaries)
    if len(free_constants) < FRAGMENT_PIXEL_CONSTANTS:
        exceeded['constants'] = FRAGMENT_PIXEL_CONSTANTS - len(free_constants)
    if vertex_slots + FRAGMENT_VERTEX_INSTRUCTIONS > VS2_INSTRUCTION_SLOTS:
        exceeded['vertex_instruction_slots'] = vertex_slots + FRAGMENT_VERTEX_INSTRUCTIONS - VS2_INSTRUCTION_SLOTS
    if structural:
        group = 'structurally_unsupported'
    elif exceeded:
        group = 'exceeds_limits'
    else:
        group = 'hostable_with_ps_2_0_fragment' if name == '2_0' else 'hostable_with_ps_2_x_fragment'
    return {'group': group, 'reasons': structural, 'exceeded': exceeded, 'pixel_profile': name,
            'position': position,
            'vertex': {'instruction_slots': vertex_slots, 'instruction_slot_limit': VS2_INSTRUCTION_SLOTS,
                       'slots_with_fragment': vertex_slots + FRAGMENT_VERTEX_INSTRUCTIONS,
                       'free_texcoord_outputs': vertex['free_texcoord_outputs'],
                       'free_color_outputs': vertex['free_color_outputs'],
                       'previous_clip_link_indices': links,
                       'constants_c252_255_free': vertex['reserved_constants_free'],
                       'relative_addressing': vertex['relative_addressing']['present'],
                       'light_loop_bound_required': vertex['relative_addressing']['present'],
                       'control_flow_counts': vertex['control_flow_counts']},
            'pixel': {'arithmetic_slots': arithmetic, 'texture_slots': texture,
                      'with_fragment': budget,
                      'free_texture_inputs': pixel['free_texture_inputs'],
                      'free_temporary_count': len(free_temporaries),
                      'three_consecutive_free_temporaries': consecutive_run(free_temporaries, 3),
                      'free_constant_count': len(free_constants),
                      'five_consecutive_free_constants': consecutive_run(free_constants, 5),
                      'color_outputs': pixel['color_outputs'],
                      'oC1_free': pixel['reserved_output_free'],
                      'texkill_dwords': pixel['texkill_dwords'],
                      'depth_output_dwords': pixel['depth_output_dwords'],
                      'control_flow_counts': pixel['control_flow_counts'],
                      'only_boolean_if': pixel['static_branches']['only_boolean_if'],
                      'predicated_or_coissued_dwords': pixel['predicated_or_coissued_dwords']}}


def effects_summary(records):
    """Where a pairing occurs: derived names and counts only."""
    return {'pass_occurrences': len(records),
            'effect_entries': len({(r['catalogue'], r['path']) for r in records}),
            'catalogues': sorted({r['catalogue'] for r in records}),
            'basenames': sorted({Path(r['path']).stem for r in records}),
            'profile_directories': sorted({r['path'].split('/')[1] for r in records}),
            'toggle_directories': sorted({'/'.join(r['path'].split('/')[2:-1]) or '(base)' for r in records}),
            'techniques': sorted({r['technique'] for r in records}),
            'pass_names': sorted({r['pass'] for r in records})}


COMPACT_DROP = {'literal_definitions', 'declarations', 'declared_inputs', 'declared_outputs',
                'declared_samplers', 'late_header_instruction_dwords', 'centroid_declarations',
                'partial_precision_declarations', 'declared_misc_registers', 'registers_referenced'}


def compact_program(digest):
    """Keep the JSON queryable at archive scale: every program summarizes its
    relative-addressing sites as base registers and counts; SM1/SM2 programs
    additionally carry only the facts the feasibility records cite."""
    if not digest.get('parsed'):
        return digest
    relative = digest['relative_addressing']
    result = dict(digest)
    result['relative_addressing'] = {
        **{key: value for key, value in relative.items() if key not in ('sites', 'note')},
        'site_count': len(relative['sites']),
        'base_registers': sorted({site['base_register'] for site in relative['sites']}),
        'destination_site_count': sum(1 for site in relative['sites'] if site.get('destination'))}
    if digest['model'] == '3_0':
        # declared_inputs/outputs/samplers already carry every declaration.
        del result['registers_referenced'], result['declarations']
        return result
    result = {key: value for key, value in result.items() if key not in COMPACT_DROP}
    site = digest.get('position_output')
    if site:
        result['position_output'] = {key: value for key, value in site.items()
                                     if key not in ('operands_xyzw', 'write_dwords')}
    result['static_branches'] = {key: value for key, value in digest['static_branches'].items()
                                 if key != 'sites'}
    return result


def model_major(model):
    return int(model.split('_')[0]) if model else None


def build(inventory, raw_directory, passes, capture=None):
    """Classify every archive pass pairing; capture counts are optional metadata."""
    programs = {p['id']: p for p in inventory['programs']}
    observed, scene_total = Counter(), 0
    if capture:
        for row in capture['pairs']:
            if row['clear_segment'] == capture['scene_segment']:
                observed[(row['vs'], row['ps'])] += row['draws']
        scene_total = capture['scene_draw_count']
    groups = {}
    for record in passes['passes']:
        groups.setdefault((record['vs'], record['ps']), []).append(record)
    profiles = {}

    def load(identifier):
        if identifier not in profiles:
            program = programs.get(identifier)
            if program is None:
                profiles[identifier] = {'id': identifier, 'parsed': False,
                                        'reason': 'absent_from_reviewed_inventory'}
            else:
                code = (raw_directory / (identifier + '.bin')).read_bytes()
                if hashlib.sha256(code).hexdigest() != program['sha256']:
                    raise ValueError('Program no longer matches reviewed sweep: ' + identifier)
                profiles[identifier] = profile(code, identifier, program['stage'], program['model'])
        return profiles[identifier]

    sm3, sm2, sm1, incomplete = [], [], [], []
    for (vs, ps), records in groups.items():
        effects = effects_summary(records)
        base = {'vs': vs, 'ps': ps, 'observed_draws': observed.get((vs, ps), 0), 'effects': effects}
        if vs is None or ps is None:
            missing = 'vs' if vs is None else 'ps'
            incomplete.append({**base, 'reason': 'pass_without_' + missing,
                               'vs_status': sorted({r['vs_status'] for r in records}),
                               'ps_status': sorted({r['ps_status'] for r in records})})
            continue
        vertex, pixel = load('vs_' + vs), load('ps_' + ps)
        vertex_major, pixel_major = model_major(vertex.get('model')), model_major(pixel.get('model'))
        models = {'vs_model': vertex.get('model'), 'ps_model': pixel.get('model')}
        if vertex_major == 3 and pixel_major == 3:
            name, blocking, differences, plan = classify(vertex, pixel)
            sm3.append({**base, **models,
                        'scene_draw_share': (round(base['observed_draws'] / scene_total, 6)
                                             if scene_total else None),
                        'vs_dword_count': vertex.get('dword_count'),
                        'ps_dword_count': pixel.get('dword_count'),
                        'transformation_class': name,
                        'position_quad_contiguous': (vertex.get('position_output') or {}).get('contiguous_quad'),
                        'hosts_reference_registers': name == 'A_reference_registers',
                        'blocking_reasons': blocking,
                        'differences_from_reference': differences,
                        'insertion_plan': plan,
                        'vs_relative_light_loop': (vertex.get('relative_addressing') or {}).get('present'),
                        'vs_loop_bound_integer_registers':
                            (vertex.get('relative_addressing') or {}).get('loop_bound_integer_registers')})
        elif vertex_major == 2 and pixel_major == 2:
            sm2.append({**base, **models, **sm2_feasibility(vertex, pixel, effects['profile_directories'])})
        elif vertex_major == 1 or pixel_major == 1:
            sm1.append({**base, **models, 'reason': SM1_REASON})
        else:
            incomplete.append({**base, **models, 'reason': 'unsupported_model_pairing'})
    for group in (sm3, sm2, sm1, incomplete):
        group.sort(key=lambda pair: (-pair['observed_draws'], pair['vs'] or '', pair['ps'] or ''))

    def summary(pairs, key):
        result = {}
        for pair in pairs:
            item = result.setdefault(pair[key], {'pairs': 0, 'observed_draws': 0, 'pass_occurrences': 0})
            item['pairs'] += 1
            item['observed_draws'] += pair['observed_draws']
            item['pass_occurrences'] += pair['effects']['pass_occurrences']
        for item in result.values():
            item['scene_share'] = round(item['observed_draws'] / scene_total, 6) if scene_total else None
        return dict(sorted(result.items()))

    outside = sorted(set(observed) - set(groups))
    return {'schema': 2,
            'scope': 'Derived motion-output insertion facts for every vertex/pixel '
                     'pairing bound by a technique pass of the installed compiled '
                     'effects. SM3 pairs are classified for the table; SM2 pairs '
                     'carry a feasibility record; SM1 pairs are counted. Offsets are '
                     'zero-based DWORD indices from the version token including '
                     'comments. Not an eligibility or equivalence proof.',
            'tool_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'inventory_sha256': inventory['_sha256'],
            'pass_table': {key: passes[key] for key in ('schema', 'scope', 'effect_count', 'pass_count',
                                                        'status_counts')},
            'capture': capture,
            'scene_draw_total': scene_total,
            'captured_pairs_outside_archive': [{'vs': vs, 'ps': ps, 'draws': observed[(vs, ps)]}
                                               for vs, ps in outside],
            'pair_count': len(sm3),
            'transformation_classes': summary(sm3, 'transformation_class'),
            'pairs': sm3,
            'unsupported_pairs': [{'vs': pair['vs'], 'ps': pair['ps'], 'observed_draws': pair['observed_draws'],
                                   'transformation_class': pair['transformation_class'],
                                   'blocking_reasons': pair['blocking_reasons'],
                                   'basenames': pair['effects']['basenames']}
                                  for pair in sm3 if pair['transformation_class'] not in HEADER_CLASSES],
            'sm2_pair_count': len(sm2),
            'sm2_limits': {'pixel_profiles': PS2_LIMITS,
                           'vertex_instruction_slots': VS2_INSTRUCTION_SLOTS,
                           'fragment': {'pixel_arithmetic': FRAGMENT_PIXEL_ARITHMETIC,
                                        'pixel_texture': FRAGMENT_PIXEL_TEXTURE,
                                        'pixel_temporaries': FRAGMENT_PIXEL_TEMPORARIES,
                                        'pixel_constants': FRAGMENT_PIXEL_CONSTANTS,
                                        'vertex_instructions': FRAGMENT_VERTEX_INSTRUCTIONS}},
            'sm2_groups': summary(sm2, 'group'),
            'sm2_reasons': dict(sorted(Counter(reason for pair in sm2 for reason in pair['reasons']).items())),
            'sm2_exceeded': dict(sorted(Counter(key for pair in sm2 for key in pair['exceeded']).items())),
            'sm2_pairs': sm2,
            'sm1_pair_count': len(sm1),
            'sm1_models': summary(sm1, 'vs_model'),
            'sm1_reason': SM1_REASON,
            'sm1_pairs': sm1,
            'incomplete_passes': incomplete,
            'programs': {identifier: compact_program(digest)
                         for identifier, digest in sorted(profiles.items())},
            'limitations': [
                'Pass pairings come from the installed archives; dynamically generated '
                'or overridden effects are not enumerated.',
                'Clear-segment phase attribution approximates the Scene phase; draw '
                'counts describe one captured session and only order the rows.',
                'Relative constant reads are bounded only by a runtime integer/loop check.',
                'Free-register lists describe static references, not driver behavior.',
                'SM2 feasibility uses documented profile maxima; device caps decide ps_2_x slots.']}


REFERENCE = {
    'vs': ('vs_53a0a641107ed76c', {'dword_count': 526, 'header_end_dword': 335,
                                  'insertion_dword': 466, 'matrix_register': 24}),
    'ps': ('ps_8759c7838bbc86c2', {'dword_count': 1260, 'definition_end_dword': 1047,
                                  'header_end_dword': 1074, 'append_dword': 1259}),
}
REFERENCE_POSITION_DWORDS = [450, 454, 458, 462]
REFERENCE_DEFINITION_DWORDS = {'vs': 302, 'ps': 1041}


def check(result):
    """Reproduce the numbers the candidate review documented for the Argon pair."""
    failures = []
    for stage, (identifier, expected) in REFERENCE.items():
        found = result['programs'].get(identifier)
        if not found:
            failures.append('missing ' + identifier)
            continue
        for key, value in expected.items():
            if found.get(key) != value:
                failures.append('%s %s=%r expected %r' % (identifier, key, found.get(key), value))
        definitions = found['literal_definitions']
        if len(definitions) != 1 or definitions[0]['dword'] != REFERENCE_DEFINITION_DWORDS[stage]:
            failures.append('%s literal def %r' % (identifier, definitions))
        if definitions and definitions[0]['dword_count'] != 6:
            failures.append('%s literal def length %r' % (identifier, definitions[0]['dword_count']))
    vertex = result['programs'].get(REFERENCE['vs'][0], {})
    site = vertex.get('position_output', {})
    if site.get('dwords_xyzw') != REFERENCE_POSITION_DWORDS:
        failures.append('position dwords %r' % site.get('dwords_xyzw'))
    if site.get('rows_xyzw') != [24, 25, 26, 27]:
        failures.append('position rows %r' % site.get('rows_xyzw'))
    if site.get('source_temporary') != 1:
        failures.append('position temporary %r' % site.get('source_temporary'))
    if not site.get('contiguous_quad') or site.get('issue_order') != 'xyzw':
        failures.append('position quad %r' % site)
    for lane, item in zip(LANES, site.get('operands_xyzw', [])):
        if (item['opcode'] != 'dp4' or item['destination']['name'] != 'o0'
                or item['destination']['mask'] != lane
                or item['sources'][0]['name'] != 'r1'
                or item['sources'][1]['name'] != 'c%d' % (24 + LANES.index(lane))
                or any(source['swizzle'] != 'xyzw' for source in item['sources'])):
            failures.append('dp4 operand %r' % item)
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--inventory', type=Path)
    parser.add_argument('--raw-directory', type=Path)
    parser.add_argument('--pass-table', type=Path,
                        help='Technique/pass pairings written by effect_passes.py.')
    parser.add_argument('--game', type=Path,
                        help='Enumerate the pass pairings from the installed archives '
                             'instead of --pass-table (read in memory; nothing is copied).')
    parser.add_argument('--capture-log', type=Path,
                        help='Optional capture log; its Scene draw counts become the '
                             'observed_draws metadata and the row order.')
    parser.add_argument('--pair-cache', type=Path,
                        help='Reuse/store the derived per-pair draw counts.')
    parser.add_argument('--from-profiles', type=Path,
                        help='Re-emit from an existing profile table instead of '
                             'rereading local bytecode and the pass table.')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--emit-header', type=Path,
                        help='Write the transformer input table '
                             '(src/renderer/motion_output_profiles_inc.h).')
    arguments = parser.parse_args()
    if arguments.from_profiles:
        result = json.loads(arguments.from_profiles.read_text())
    else:
        missing = [name for name in ('inventory', 'raw_directory')
                   if getattr(arguments, name) is None]
        if missing or not (arguments.pass_table or arguments.game):
            parser.error('--from-profiles, or --inventory, --raw-directory and one of '
                         '--pass-table / --game')
        inventory_bytes = arguments.inventory.read_bytes()
        inventory = json.loads(inventory_bytes)
        inventory['_sha256'] = hashlib.sha256(inventory_bytes).hexdigest()
        if arguments.pass_table:
            passes = json.loads(arguments.pass_table.read_text())
        else:
            from effect_passes import archive_passes
            passes = archive_passes(arguments.game)
        capture = None
        if arguments.pair_cache and arguments.pair_cache.exists():
            capture = json.loads(arguments.pair_cache.read_text())
        elif arguments.capture_log:
            capture = capture_pairs(arguments.capture_log)
            if arguments.pair_cache:
                arguments.pair_cache.write_text(json.dumps(capture, indent=2) + '\n')
        result = build(inventory, arguments.raw_directory, passes, capture)
    failures = check(result)
    result['reference_check'] = {'pair': 'argon_sm3',
                                 'passed': not failures, 'failures': failures}
    if arguments.output:
        # Archive scale: compact, stable JSON like the sweep inventories; query
        # it with scripts or the paired tests rather than reading it whole.
        arguments.output.write_text(json.dumps(result, separators=(',', ':')) + '\n')
    if arguments.emit_header:
        arguments.emit_header.write_text(render_header(result))
    print(json.dumps({'sm3_pairs': result['pair_count'],
                      'scene_draws': result['scene_draw_total'],
                      'classes': result['transformation_classes'],
                      'unsupported_sm3_pairs': len(result['unsupported_pairs']),
                      'sm2_pairs': result['sm2_pair_count'],
                      'sm2_groups': result['sm2_groups'],
                      'sm2_reasons': result['sm2_reasons'],
                      'sm2_exceeded': result['sm2_exceeded'],
                      'sm1_pairs': result['sm1_pair_count'],
                      'sm1_models': result['sm1_models'],
                      'incomplete_passes': len(result['incomplete_passes']),
                      'header_rows': len(header_rows(result)),
                      'reference_check': result['reference_check']}, indent=2))
    return 0 if not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
