#!/usr/bin/env python3
"""Certify bounded original SM3 DEFAULT material conversion sites, without rewriting shaders.

Input: complete local archive programs and the existing derived motion inventory.
Output: fingerprints, semantic operand locations and available resources only.
Comments (including preshaders) are opaque. Exact whole-program hashes bind the
manual semantic review; there is no motif-only admission or hash override.
Transfer policy, new instructions and native/GPU equivalence remain future work.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inspect_motion_output_profiles as motion

# Derived whole-program identities from the reviewed archive, not shader words.
ORIGINALS = {
    'vs_53a0a641107ed76c': ('bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c', 526),
    'vs_719856ce0c213220': ('1aa39cbd8137cfcf9fb664451cf8c21060e0093c574889749afd8e69b17c19b9', 526),
    'vs_badefd5143b3024f': ('bd820507d47cfc99b3c182d7a86ac91ea2812811f5f5d53be603b023bfb43448', 481),
    'ps_8759c7838bbc86c2': ('9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0', 1260),
    'ps_63f96eba9eea7880': ('2046f15c8d3c761508a5abf8e4b540c7d6aa8902d131f02533377e68a9bfa75b', 1292),
    'ps_593e5dea9b3457d5': ('4e52664af108eddc19c13a98c8da62f539c79a1d085fbc55676e6130220907b2', 264),
    'ps_7a0bb00a8070496a': ('5b1aa3fa94f127c6b36164cb164b114e97d279007ccd882416f2d6b9cd01c8fd', 1215),
    'ps_8d5b2ba0fb4d13bf': ('4f1a61cf0fb97d6328ede3e18d2b715727938062ae601e180382380a3279c4f0', 1183),
    'ps_dab93928f26906f7': ('b15be08d6adc08a50c9ac756300f091c9fb1a3ec756fb65a556cc523a5166221', 296),
    'ps_3b94320087e81945': ('f4f228d05aef995c3a24a74ee4a494763103511f188a60cd695d8a1ec392a12c', 1264),
    'ps_e3b7acc16da9932d': ('db4c2d86eddbb7a60ba69d55c10dc8f58bb1dadd123696ced0d093f5b4630c15', 1296),
    'ps_7a14d4dcb28f27e5': ('ad5d2399d2f0dae0a8721eb2d33adee0fa1cc90d752d523aa18f63923886d2a3', 1187),
    'ps_8ab6188a40ca15ea': ('0c6703bacf857148b3bf528e8d8814b05a2332ba1889f3274148c2283d73ce3e', 1219),
    'ps_8df6143d0e77d92e': ('a2a2cc4d1b2f1ea0b6ff7b2382f41742c4b1b7bbe1706806ee2312ec0f88e821', 268),
    'ps_e16a9806ee3544c3': ('85b9538879c97626c5181f24c07585810e99b13001c6e0c50d5b1b695a5bfee4', 300),
}
# Reviewed semantic sites: sampler instruction order s0/s1/s2/s3, affine RGB
# completion (None when absent), COLOR0 clamp, directional register:consumers,
# final RGB output. Each offset addresses the ORIGINAL stream.
PIXELS = {
    '8759c7838bbc86c2': ((1197, 1175, 1242, 1229), 1217, 1206, {5: (1170, 1183), 7: (1158, 1166)}, 1251),
    '63f96eba9eea7880': ((1229, 1207, 1274, 1261), 1249, 1238, {5: (1202, 1215), 7: (1190, 1198)}, 1283),
    '593e5dea9b3457d5': ((225, 204, 246, 233), None, 217, {2: (220,)}, 255),
    '7a0bb00a8070496a': ((1151, 1138, 1197, 1184), 1171, 1160, {5: (1175,)}, 1206),
    '8d5b2ba0fb4d13bf': ((1119, 1106, 1165, 1152), 1139, 1128, {5: (1143,)}, 1174),
    'dab93928f26906f7': ((257, 236, 278, 265), None, 249, {2: (252,)}, 287),
    '3b94320087e81945': ((1192, 1175, 1246, 1233), 1214, 1218, {5: (1170, 1183), 7: (1158, 1166)}, 1255),
    'e3b7acc16da9932d': ((1224, 1207, 1278, 1265), 1246, 1250, {5: (1202, 1215), 7: (1190, 1198)}, 1287),
    '7a14d4dcb28f27e5': ((1114, 1106, 1169, 1156), 1136, 1140, {5: (1147,)}, 1178),
    '8ab6188a40ca15ea': ((1146, 1138, 1201, 1188), 1168, 1172, {5: (1179,)}, 1210),
    '8df6143d0e77d92e': ((220, 204, 250, 237), None, 217, {2: (228,)}, 259),
    'e16a9806ee3544c3': ((252, 236, 282, 269), None, 249, {2: (260,)}, 291),
}
BASE_VS = '53a0a641107ed76c'
TOGGLE_VS = ('719856ce0c213220', 'badefd5143b3024f')
FAMILIES = {
    'argon': {'pixels': list(PIXELS)[:6], 'production_status': 'qualified_a56e77e',
              'aliases': ['argon'], 'coefficients': {'diffuse': 0.4000000059604645, 'specular_power': 5, 'cube': 1.0}},
    'shared_default': {'pixels': list(PIXELS)[6:], 'production_status': 'offline_proof_only',
                       'aliases': ['khaak', 'teladi', 'teladi_nodiff', 'xenon'],
                       'coefficients': {'diffuse': 0.5, 'specular_power': 6, 'cube': 0.5}},
}
PIXEL_FAMILY = {key: family for family, record in FAMILIES.items() for key in record['pixels']}
PAIRS = {(BASE_VS, key) for record in FAMILIES.values() for key in record['pixels'][:2]} | {
    (vs, key) for vs in TOGGLE_VS for record in FAMILIES.values() for key in record['pixels'][2:]}



def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def decode_sites(items):
    """Attach original DWORD positions to the reused, relative-aware decoder."""
    result = {}
    for item in items:
        dest, sources = motion.split_operands(item, 3)
        at = item['dword'] + 1
        if dest:
            dest = dict(dest, operand_dword=at)
            at += 1 + int(dest['relative'])
        located = []
        for source in sources:
            token = item['words'][at - item['dword'] - 1]
            located.append(dict(source, operand_dword=at,
                                source_modifier=(token >> 24) & 15,
                                **({'relative_operand_dword': at + 1} if source['relative'] else {})))
            at += 1 + int(source['relative'])
        result[item['dword']] = {'item': item, 'destination': dest, 'sources': located}
    return result


def compact_operand(value):
    if value is None:
        return None
    return {key: item for key, item in value.items()
            if key not in ('register', 'register_type') and (key != 'relative' or item)}


def site(decoded, offset):
    require(offset in decoded, f'conversion site {offset} is not an instruction')
    row = decoded[offset]
    return {'instruction_dword': offset, 'opcode': motion.OPCODES[row['item']['opcode']], 'end_dword': offset + 1 + row['item']['length'],
            'destination': compact_operand(row['destination']),
            'sources': [compact_operand(source) for source in row['sources']]}


def uses(decoded, name):
    return [(offset, source) for offset, row in decoded.items()
            for source in row['sources'] if source['name'] == name]


def source_role(decoded, name, expected_offsets, relative=False):
    references = uses(decoded, name)
    require([at for at, _ in references] == sorted(expected_offsets), f'{name} source inventory changed')
    require(all(s['relative'] == relative and s['swizzle'] == 'xyzw' and
                s['source_modifier'] == 0 for _, s in references), f'{name} source shape changed')
    return [dict(instruction_dword=at, opcode=motion.OPCODES[decoded[at]['item']['opcode']],
                 consumer_destination=compact_operand(decoded[at]['destination']), **compact_operand(source))
            for at, source in references]


def ranges(numbers):
    """Inclusive ranges keep large, contiguous free constant sets compact."""
    result = []
    for number in sorted(numbers):
        if result and result[-1][1] + 1 == number:
            result[-1][1] = number
        else:
            result.append([number, number])
    return result


def budget(profile, stage, loop):
    reserved_temps = {5, 6, 7} if stage == 'ps' else set()
    temporal_constants = set(range(216, 221) if stage == 'ps' else range(252, 256))
    material_constants = {212, 213} if stage == 'ps' else {248, 249}
    require(not temporal_constants & material_constants, 'material/temporal constant ABI collision')
    reserved_constants = temporal_constants | material_constants
    used_constants = set(profile['constant_registers_direct']) | set(profile['defined_constant_registers'])
    if loop:
        used_constants.update(range(24))  # Conditional on the existing i0 [0,8] draw gate.
    require(not reserved_constants & used_constants, 'constant ABI collision')
    require(not reserved_temps & set(profile['temporary_registers']), 'temporary ABI collision')
    free_temps = set(range(32)) - set(profile['temporary_registers']) - reserved_temps
    # Current-depth reuses the first motion temporary after motion has finished.
    free_io = set(profile['free_input_registers' if stage == 'ps' else 'free_output_registers'])
    reserved_io = {5, 6, 7} if stage == 'ps' else {6, 7, 8}
    require(reserved_io <= free_io, 'motion/depth/material interpolator ABI collision')
    original_semantics = set(profile['declared_texcoord_input_indices' if stage == 'ps'
                                     else 'declared_texcoord_output_indices'])
    require(6 not in original_semantics, 'material TEXCOORD6 ABI collision')
    used_semantics = original_semantics | {4, 5, 6}
    executable = {name: count for name, count in profile['opcode_counts'].items()
                  if name not in ('dcl', 'def', 'defi', 'defb')}
    return {'original_temporaries': profile['temporary_registers'],
            'free_temporary_ranges': ranges(free_temps),
            'free_constant_ranges': ranges(set(range(224 if stage == 'ps' else 256)) - used_constants - reserved_constants),
            'free_interpolator_registers': sorted(free_io - reserved_io),
            'free_texcoord_semantic_indices': sorted(set(range(16)) - used_semantics),
            'original_executable_instruction_count': sum(executable.values()),
            'original_static_slot_estimate': sum(count * motion.SLOT_COSTS.get(name, 1) for name, count in executable.items()),
            'instruction_budget_note': 'Static original estimate only; repeated VS loop work and future transfer/motion/depth instructions are not included. Check final bytecode against device caps.',
            'relative_constant_bound_required': loop,
            'material_resources_proven_free_before_reservation': {'rgb_interpolator_register': 7 if stage == 'ps' else 8,
                                                                 'rgb_texcoord_index': 6,
                                                                 'def_constants': sorted(material_constants)},
            'reservations_apply_to_current_depth_modes': [False, True]}


# Compact expected operand shapes are reviewed technical contracts, not bytecode.
PP = ('partial_precision',)


def expect(decoded, at, opcode, destination, sources, modifiers=()):
    """Check the opcode, complete operands, modifiers and their DWORD positions."""
    row = site(decoded, at)
    require(row['opcode'] == opcode, f'{at}: opcode changed')
    dst = row['destination']
    require((None if dst is None else (dst['name'], dst['mask'])) == destination,
            f'{at}: destination changed')
    cursor = at + 1
    if dst:
        require(not dst.get('relative') and dst['operand_dword'] == cursor and
                dst['modifiers'] == sorted(modifiers), f'{at}: destination flags/offset changed')
        cursor += 1
    require(len(row['sources']) == len(sources), f'{at}: source count changed')
    for actual, expected in zip(row['sources'], sources):
        # (register, swizzle, modifier=0, relative-address-register=None, lane=None)
        expected = tuple(expected) + (0, None, None)[max(0, len(expected) - 2):]
        name, swizzle, modifier, address, component = expected
        require((actual['name'], actual['swizzle'], actual['source_modifier'],
                 actual.get('address_register'), actual.get('address_component')) ==
                (name, swizzle, modifier, address, component), f'{at}: source shape changed')
        require(actual.get('relative', False) == bool(address) and actual['operand_dword'] == cursor,
                f'{at}: source relative/offset changed')
        if address:
            require(actual['relative_operand_dword'] == cursor + 1, f'{at}: address offset changed')
        cursor += 1 + bool(address)
    require(row['end_dword'] == cursor, f'{at}: instruction length changed')
    return row


def lane_writes(decoded, name, lane=None, begin=-1, end=1 << 30):
    return [at for at, row in decoded.items() if begin < at < end and row['destination'] and
            row['destination']['name'] == name and
            (lane is None or lane in row['destination']['mask'])]


def no_lane_writes(decoded, name, lane, begin, end):
    require(not lane_writes(decoded, name, lane, begin, end), f'{name}.{lane}: intervening write')


def prove_vertex(decoded, loop):
    """Prove point addressing and the complete COLOR0/alpha producer chain."""
    if loop:
        definition = [row['item'] for row in decoded.values() if row['item']['opcode'] == motion.DEF and
                      motion.register_of(row['item']['words'][0]) == (2, 42)]
        require(len(definition) == 1 and len(definition[0]['words']) == 5, 'loop stride definition missing/malformed')
        import struct
        values = struct.unpack('<4f', struct.pack('<4I', *definition[0]['words'][1:]))
        require(values == (1.0, 0.0, 3.0, 0.0), 'loop stride/initialization literal changed')
        expected = [
            (378, 'mov', ('r0', 'xyz'), [('c42', 'yyyy')]),
            (381, 'mov', ('r0', 'w'), [('c42', 'yyyy')]),
            (384, 'rep', None, [('i0', 'xyzw')]),
            (386, 'mul', ('r2', 'w'), [('r0', 'wwww'), ('c42', 'zzzz')]),
            (390, 'mova', ('a0', 'w'), [('r2', 'wwww')]),
            (393, 'add', ('r6', 'xyz'), [('r3', 'xyzw', 1), ('c0', 'xyzw', 0, 'a0', 'w')]),
            (420, 'dp3', ('r2', 'w'), [('c2', 'xyzw', 0, 'a0', 'w'), ('r5', 'xyzw')]),
            (428, 'mul', ('r5', 'xyz'), [('r3', 'wwww'), ('c1', 'xyzw', 0, 'a0', 'w')]),
            (433, 'mad', ('r0', 'xyz'), [('r5', 'xyzw'), ('r2', 'wwww'), ('r0', 'xyzw')]),
            (438, 'add', ('r0', 'w'), [('r0', 'wwww'), ('c42', 'xxxx')]),
            (442, 'endrep', None, []),
            (443, 'add', ('o1', 'xyz'), [('r0', 'xyzw'), ('c40', 'xyzw')]),
        ]
        for args in expected:
            expect(decoded, *args)
        relative = [(at, src['name'], src.get('address_register'), src.get('address_component'))
                    for at, row in decoded.items() for src in row['sources'] if src['relative']]
        require(relative == [(393, 'c0', 'a0', 'w'), (420, 'c2', 'a0', 'w'), (428, 'c1', 'a0', 'w')],
                'relative point inventory changed')
        require(lane_writes(decoded, 'a0') == [390], 'point address overwritten')
        require(lane_writes(decoded, 'r0', 'w', 380, 443) == [381, 438], 'loop counter overwritten')
        require(lane_writes(decoded, 'r0', None, 377, 443) == [378, 381, 433, 438], 'loop accumulator overwritten')
        require([at for at, _ in uses(decoded, 'i0')] == [384], 'loop count source changed')
        proof = {'rep_dword': 384, 'endrep_dword': 442, 'counter_initialization_dword': 381,
                 'counter_increment_dword': 438, 'stride_multiply_dword': 386, 'address_write_dword': 390,
                 'stride_definition_dword': definition[0]['dword'],
                 'stride_literal_dword': definition[0]['dword'] + 4, 'verified_stride': 3,
                 'relative_source_dwords': [396, 422, 431], 'runtime_count_range_required': [0, 8]}
        shift, alpha_c, fog_c, rgb = 0, 'c39', 'c41', 443
    else:
        expect(decoded, 389, 'mul', ('r1', 'xyz'), [('r1', 'zzzz'), ('c5', 'xyzw')])
        expect(decoded, 397, 'mad', ('o1', 'xyz'), [('r1', 'xyzw'), ('r1', 'wwww'), ('c19', 'xyzw')])
        require(not any(src['relative'] for row in decoded.values() for src in row['sources']), 'fixed point became relative')
        proof = {'model': 'fixed_single_point', 'relative_sources': 0}
        shift, alpha_c, fog_c, rgb = -45, 'c18', 'c20', 397
    expect(decoded, 483 + shift, 'if', None, [('b0', 'xyzw')])
    expect(decoded, 485 + shift, 'dp3', ('r0', 'w'), [('r0', 'xyzw'), ('r0', 'xyzw')])
    expect(decoded, 489 + shift, 'rsq', ('r0', 'w'), [('r0', 'wwww')])
    expect(decoded, 492 + shift, 'rcp', ('r0', 'w'), [('r0', 'wwww')])
    expect(decoded, 495 + shift, 'mad', ('r0', 'w'), [(fog_c, 'yyyy'), ('r0', 'wwww', 1), (fog_c, 'xxxx')], ('saturate',))
    expect(decoded, 500 + shift, 'mul', ('o1', 'w'), [('r0', 'wwww'), (alpha_c, 'xxxx')])
    expect(decoded, 504 + shift, 'else', None, [])
    expect(decoded, 505 + shift, 'mov', ('o1', 'w'), [(alpha_c, 'xxxx')])
    expect(decoded, 508 + shift, 'endif', None, [])
    require(lane_writes(decoded, 'o1') == [rgb, 500 + shift, 505 + shift], 'COLOR0 output inventory changed')
    flow = [(at, motion.OPCODES[row['item']['opcode']]) for at, row in decoded.items()
            if motion.OPCODES[row['item']['opcode']] in motion.FLOW_OPCODES]
    require(flow == ([(384, 'rep'), (442, 'endrep')] if loop else []) +
            [(483 + shift, 'if'), (504 + shift, 'else'), (508 + shift, 'endif')], 'vertex flow changed')
    return proof


def prove_pixel(decoded, key):
    """Prove sampled alpha liveness and exact RGB/final output source shapes."""
    tex, affine, clamp, direct, final = PIXELS[key]
    shared = PIXEL_FAMILY[key] == 'shared_default'
    clamp_register = 'r2' if shared and (len(direct) == 2 or not affine) else 'r1'
    for sampler, at in enumerate(tex):
        expect(decoded, at, 'texld', ('r1' if sampler == 0 else 'r0', 'xyzw'),
               [('v4' if sampler == 3 else 'v1', 'xyzw'), (f's{sampler}', 'xyzw')], PP)
    expect(decoded, clamp, 'mov', (clamp_register, 'xyz'), [('v0', 'xyzw')], PP + ('saturate',))
    affine_sites = []
    if affine:
        for lane, at, constant in zip('xyz', (affine - 8, affine - 4, affine), range(3)):
            affine_sites.append(expect(decoded, at, 'dp4', ('r3', lane), [('r2', 'xyzw'), (f'c{constant}', 'xyzw')], PP))
        no_lane_writes(decoded, 'r3', 'x', affine - 8, affine + 4)
        no_lane_writes(decoded, 'r3', 'y', affine - 4, affine + 4)
    # Both base directional branches independently consume each light RGB.
    if len(direct) == 2:
        shift = final - (1255 if shared else 1251)
        for at, op, dst, sources in [
            (1158, 'mul', ('r0', 'xyz'), [('r1', 'yyyy'), ('c7', 'xyzw')]),
            (1166, 'mul', ('r2' if shared else 'r1', 'xyz'), [('r2', 'wwww'), ('c7', 'xyzw')]),
            (1170, 'mad', ('r1' if shared else 'r2', 'xyz'), [('r0', 'wwww'), ('c5', 'xyzw'), ('r0', 'xyzw')]),
            (1183, 'mad', ('r3' if shared else 'r1', 'xyz'), [('r1', 'wwww'), ('c5', 'xyzw'), ('r2' if shared else 'r1', 'xyzw')]),
        ]:
            expect(decoded, at + shift, op, dst, sources, PP)
    else:
        constant, offsets = next(iter(direct.items()))
        expect(decoded, offsets[0], 'mad', ('r1' if affine else 'r2', 'xyz'),
               [('r0', 'wwww'), (f'c{constant}', 'xyzw'), (clamp_register, 'xyzw')], PP)
    lrp = tex[2] + 4
    expect(decoded, lrp, 'lrp', ('r2', 'w'),
           [('c3' if affine else 'c0', 'xxxx'), ('r0', 'wwww'), ('r1', 'wwww')], PP)
    no_lane_writes(decoded, 'r1', 'w', tex[0], lrp)
    no_lane_writes(decoded, 'r0', 'w', tex[2], lrp)
    expect(decoded, final, 'add', ('oC0', 'xyz'), [('r1', 'xyzw'), ('r0', 'xyzw')], PP)
    expect(decoded, final + 4, 'mul', ('oC0', 'w'), [('r2', 'wwww'), ('v0', 'wwww')], PP)
    no_lane_writes(decoded, 'r2', 'w', lrp, final + 4)
    outputs = [(at, row['destination']['name'], row['destination']['mask']) for at, row in decoded.items()
               if row['destination'] and row['destination']['register_type'] in (4, 5, 6, 8, 9)]
    require(outputs == [(final, 'oC0', 'xyz'), (final + 4, 'oC0', 'w')], 'pixel output inventory changed')
    require(not any(motion.OPCODES[row['item']['opcode']] in motion.FLOW_OPCODES
                    for row in decoded.values()), 'pixel alpha liveness requires straight-line flow')
    return {'diffuse_alpha_live_interval': [tex[0], lrp], 'lightmap_alpha_live_interval': [tex[2], lrp],
            'interpolated_alpha_live_interval': [lrp, final + 4], 'affine_rgb_sites': affine_sites}


def literal_component(decoded, register, component, expected):
    """Read an actual DEF component, never CTAB/preshader or application data."""
    import struct
    definitions = [row['item'] for row in decoded.values() if row['item']['opcode'] == motion.DEF and
                   motion.name_of(*motion.register_of(row['item']['words'][0])) == register]
    require(len(definitions) == 1 and len(definitions[0]['words']) == 5, 'lobe definition missing')
    definition = definitions[0]
    value = struct.unpack('<f', struct.pack('<I', definition['words'][1 + 'xyzw'.index(component)]))[0]
    require(value == expected, 'lobe coefficient literal changed')
    return {'register': register, 'component': component, 'definition_dword': definition['dword'],
            'literal_dword': definition['dword'] + 2 + 'xyzw'.index(component), 'value': value}


def sixth_power(decoded, dot, multiplies, consume):
    """Prove the reviewed scalar x²/x⁴/x⁶ chain and its last live consumer."""
    dot_site = expect(decoded, dot, 'dp3', ('r0', 'w'), [('r1', 'xyzw'), ('r2', 'xyzw')], PP + ('saturate',))
    live = {('r0', 'w'): (1, dot)}
    sites = []
    for at, expected_power in zip(multiplies, (2, 4, 6)):
        row = site(decoded, at)
        destination = row['destination']
        require(row['opcode'] == 'mul' and destination and len(destination['mask']) == 1 and
                destination['modifiers'] == list(PP) and len(row['sources']) == 2,
                'specular power instruction changed')
        exponents = []
        for source in row['sources']:
            swizzle = source['swizzle']
            lane = (source['name'], swizzle[0])
            require(len(set(swizzle)) == 1 and not source.get('relative') and
                    source['source_modifier'] == 0 and lane in live, 'specular power source changed')
            exponent, writer = live[lane]
            no_lane_writes(decoded, lane[0], lane[1], writer, at)
            exponents.append(exponent)
        require(sum(exponents) == expected_power, 'specular power exponent changed')
        result_lane = (destination['name'], destination['mask'])
        live[result_lane] = (expected_power, at)
        sites.append(row)
    no_lane_writes(decoded, result_lane[0], result_lane[1], multiplies[-1], consume)
    consumer = site(decoded, consume)
    require(any(source['name'] == result_lane[0] and source['swizzle'] == result_lane[1] * 4
                and source['source_modifier'] == 0 for source in consumer['sources']), 'sixth power result not consumed')
    return {'power': 6, 'dot': dot_site, 'multiply_sites': sites,
            'response_multiply': consumer, 'result_live_until_dword': consume}


def prove_shared_lobe(decoded, key):
    """Six individually bound shared-family shapes; no Argon offset reuse."""
    require(PIXEL_FAMILY[key] == 'shared_default', 'not the shared DEFAULT family')
    base = key in ('3b94320087e81945', 'e3b7acc16da9932d')
    tex, affine, _, _, _ = PIXELS[key]
    if base:
        shift = 0 if key == '3b94320087e81945' else 32
        half_reg, half_lane = ('c8', 'w') if shift == 0 else ('c9', 'x')
        three_reg, three_lane = 'c8', 'z' if shift == 0 else 'w'
        diffuse = expect(decoded, 1201 + shift, 'mad', ('r1', 'xyz'),
                         [('r3', 'xyzw'), (half_reg, half_lane * 4), ('r4', 'xyzw')], PP)
        cube = expect(decoded, 1229 + shift, 'mul', ('r2', 'xyz'),
                      [('r0', 'xyzw'), (half_reg, half_lane * 4)], PP)
        expect(decoded, 1126 + shift, 'mul', ('r3', 'w'), [('r2', 'wwww'), (three_reg, three_lane * 4)], PP + ('saturate',))
        expect(decoded, 1154 + shift, 'mul', ('r1', 'z'), [('r1', 'wwww'), (three_reg, three_lane * 4)], PP + ('saturate',))
        expect(decoded, 1179 + shift, 'mul', ('r0', 'w'), [('r0', 'xxxx'), (three_reg, three_lane * 4)], PP)
        expect(decoded, 1134 + shift, 'mul', ('r1', 'y'), [('r1', 'wwww'), ('r3', 'wwww')], PP)
        expect(decoded, 1162 + shift, 'mul', ('r0', 'w'), [('r0', 'wwww'), ('r1', 'zzzz')], PP)
        powers = [sixth_power(decoded, 1093 + shift, [at + shift for at in (1097, 1101, 1109)], 1134 + shift),
                  sixth_power(decoded, 1130 + shift, [at + shift for at in (1138, 1146, 1150)], 1162 + shift)]
    else:
        # dot, half DEF, packed swizzle, specular-strength DEF, lobe combine, cube scale
        facts = {
            '7a14d4dcb28f27e5': (1075, 'c6', 'w', 'zwzw', 'c6', 'z', 1123, 1152),
            '8ab6188a40ca15ea': (1107, 'c6', 'y', 'xyzw', 'c7', 'w', 1155, 1184),
            '8df6143d0e77d92e': (173, 'c3', 'y', 'xyzw', 'c3', 'x', 212, 233),
            'e16a9806ee3544c3': (205, 'c4', 'y', 'xyzw', 'c3', 'w', 244, 265),
        }
        dot, half_reg, half_lane, packed_swizzle, three_reg, three_lane, lobe, cube_at = facts[key]
        packed = 'r3' if affine else 'r1'
        diffuse = expect(decoded, dot + 16, 'mul', (packed, 'xy'), [('r0', 'yyyy'), (half_reg, packed_swizzle)], PP)
        # The packed X lane and the later mask multiply both use literal three.
        packed_three = literal_component(decoded, half_reg, packed_swizzle[0], 3.0)
        expect(decoded, dot + 24, 'mov', ('r0', 'w'), [(packed, 'xxxx')], PP + ('saturate',))
        expect(decoded, dot + 27, 'mul', ('r1', 'w'), [('r0', 'zzzz'), ('r0', 'wwww')], PP)
        expect(decoded, lobe, 'mad', ('r0', 'w'), [('r0', 'wwww'), (three_reg, three_lane * 4), (packed, 'yyyy')], PP)
        cube = expect(decoded, cube_at, 'mul', ('r2' if affine else 'r3', 'xyz'),
                      [('r0', 'xyzw'), (half_reg, half_lane * 4)], PP)
        powers = [sixth_power(decoded, dot, [dot + 8, dot + 12, dot + 20], dot + 27)]
    # The cube scalar modifies mask*albedo before the s3 sample multiply.
    pre_cube = cube['instruction_dword'] - (8 if base else 9)
    # Base: mask*albedo is eight DWORDs before cube scale. Single: the
    # intervening directional MAD is five DWORDs, so the gap is nine.
    expect(decoded, pre_cube, 'mul', ('r0', 'xyz'), [('r0', 'xxxx'), ('r3' if affine else 'r1', 'xyzw')], PP)
    expect(decoded, tex[3] + 4, 'mul', ('r0', 'xyz'), [('r2' if affine else 'r3', 'xyzw'), ('r0', 'xyzw')], PP)
    # Prove the links as well as isolated coefficients: sampled s1.x remains
    # live until both specular weighting and mask*albedo, and every diffuse /
    # specular producer survives until the exact consumer named below.
    links = []

    def live(register, lanes, producer, consumer, role):
        for lane in lanes:
            no_lane_writes(decoded, register, lane, producer, consumer)
        links.append({'role': role, 'register': register, 'lanes': lanes,
                      'producer_dword': producer, 'consumer_dword': consumer})

    expect(decoded, tex[1], 'texld', ('r0', 'xyzw'), [('v1', 'xyzw'), ('s1', 'xyzw')], PP)
    expect(decoded, tex[3], 'texld', ('r0', 'xyzw'), [('v4', 'xyzw'), ('s3', 'xyzw')], PP)
    live('r0', 'x', tex[1], pre_cube, 'specular_mask_to_reflection')
    live('r0', 'xyz', pre_cube, cube['instruction_dword'], 'masked_albedo_to_cube_scale')
    live('r2' if affine else 'r3', 'xyz', cube['instruction_dword'], tex[3] + 4, 'scaled_albedo_to_cube_sample')
    if base:
        expect(decoded, 1117 + shift, 'dp3', ('r2', 'w'), [('r0', 'xyzw'), ('c6', 'xyzw')], PP + ('saturate',))
        expect(decoded, 1142 + shift, 'dp3', ('r1', 'w'), [('r0', 'xyzw'), ('c4', 'xyzw')], PP + ('saturate',))
        expect(decoded, 1158 + shift, 'mul', ('r0', 'xyz'), [('r1', 'yyyy'), ('c7', 'xyzw')], PP)
        expect(decoded, 1166 + shift, 'mul', ('r2', 'xyz'), [('r2', 'wwww'), ('c7', 'xyzw')], PP)
        expect(decoded, 1170 + shift, 'mad', ('r1', 'xyz'), [('r0', 'wwww'), ('c5', 'xyzw'), ('r0', 'xyzw')], PP)
        expect(decoded, 1183 + shift, 'mad', ('r3', 'xyz'), [('r1', 'wwww'), ('c5', 'xyzw'), ('r2', 'xyzw')], PP)
        expect(decoded, 1188 + shift, 'mul', ('r4', 'xyz'), [('r1', 'xyzw'), ('r0', 'wwww')], PP)
        for register, lanes, producer, consumer, role in (
            ('r2', 'w', 1117, 1126, 'light1_cosine_to_response'),
            ('r3', 'w', 1126, 1134, 'light1_response_to_sixth_power'),
            ('r2', 'w', 1117, 1166, 'light1_cosine_to_diffuse_rgb'),
            ('r1', 'w', 1142, 1154, 'light0_cosine_to_response'),
            ('r1', 'z', 1154, 1162, 'light0_response_to_sixth_power'),
            ('r1', 'w', 1142, 1183, 'light0_cosine_to_diffuse_rgb'),
            ('r1', 'y', 1134, 1158, 'light1_lobe_to_specular_rgb'),
            ('r0', 'w', 1162, 1170, 'light0_lobe_to_specular_rgb'),
            ('r0', 'xyz', 1158, 1170, 'light1_specular_to_rgb_sum'),
            ('r2', 'xyz', 1166, 1183, 'light1_diffuse_to_rgb_sum'),
            ('r1', 'xyz', 1170, 1188, 'specular_rgb_sum_to_mask'),
            ('r0', 'x', 1175, 1179, 'specular_mask_to_strength'),
            ('r0', 'w', 1179, 1188, 'scaled_mask_to_specular_rgb'),
            ('r3', 'xyz', 1183, 1201, 'diffuse_rgb_sum_to_half_scale'),
            ('r4', 'xyz', 1188, 1201, 'masked_specular_rgb_to_lobe_sum'),
        ):
            live(register, lanes, producer + shift, consumer + shift, role)
    else:
        direction = 'c4' if affine else 'c1'
        expect(decoded, dot + 4, 'dp3', ('r0', 'y'), [('r0', 'xyzw'), (direction, 'xyzw')], PP + ('saturate',))
        expect(decoded, tex[1] + 4, 'mul', ('r0', 'w'), [('r1', 'wwww'), ('r0', 'xxxx')], PP)
        direct_register, direct_sites = next(iter(PIXELS[key][3].items()))
        direct_at = direct_sites[0]
        color_sum = 'r1' if affine else 'r2'
        expect(decoded, direct_at, 'mad', (color_sum, 'xyz'),
               [('r0', 'wwww'), (f'c{direct_register}', 'xyzw'), (color_sum, 'xyzw')], PP)
        live('r0', 'y', dot + 4, dot + 16, 'cosine_to_packed_diffuse_response')
        live(packed, 'x', dot + 16, dot + 24, 'packed_response_to_saturation')
        live(packed, 'y', dot + 16, lobe, 'half_diffuse_to_lobe_sum')
        live('r0', 'w', dot + 24, dot + 27, 'saturated_response_to_sixth_power')
        live('r1', 'w', dot + 27, tex[1] + 4, 'specular_response_to_mask')
        live('r0', 'x', tex[1], tex[1] + 4, 'specular_mask_to_response')
        live('r0', 'w', tex[1] + 4, lobe, 'masked_specular_to_strength_and_diffuse_sum')
        live('r0', 'w', lobe, direct_at, 'lobe_sum_to_light_rgb')
    result = {'verified_producer_consumer_links': links,
              'diffuse_and_cube_literal': literal_component(decoded, half_reg, half_lane, 0.5),
              'specular_strength_literal': literal_component(decoded, three_reg, three_lane, 3.0),
              'diffuse_coefficient_site': diffuse, 'cube_coefficient_site': cube, 'specular_power_chains': powers}
    if not base:
        result['packed_response_three_literal'] = packed_three
    return result


def inspect_program(code, identifier):
    require(identifier in ORIGINALS, 'unreviewed original')
    digest, count = ORIGINALS[identifier]
    require(len(code) == count * 4 and hashlib.sha256(code).hexdigest() == digest and
            motion.fnv1a64(code) == identifier[3:], 'original fingerprint/count mismatch')
    stage, key = identifier.split('_')
    words, items, end = motion.instructions(code)
    require(words[0] == (0xfffe0300 if stage == 'vs' else 0xffff0300), 'not original SM3 stage')
    require(all(not i['predicated'] and not i['coissued'] for i in items), 'unsupported predication/coissue')
    decoded = decode_sites(items)
    profile = motion.profile(code, identifier, stage, '3_0')
    require(profile['parsed'] and profile['header_is_contiguous'] and profile['control_flow_balanced'], 'invalid original structure')
    loop = stage == 'vs' and key != 'badefd5143b3024f'
    output = {'id': identifier, 'families': list(FAMILIES) if stage == 'vs' else [PIXEL_FAMILY[key]], 'fnv1a64': key, 'sha256': digest, 'word_count': count,
              'header_end_dword': profile['header_end_dword'], 'end_dword': end,
              'opaque_comment_dword_count': count - 2 - sum(i['length'] + 1 for i in items),
              'budget': budget(profile, stage, loop),
              'motion_splice': ({'declaration_insert_dword': profile['header_end_dword'],
                                  'arithmetic_insert_dword': profile['position_output']['insertion_dword'],
                                  'position_source_temporary': profile['position_output']['source_temporary'],
                                  'position_dp4_dwords': profile['position_output']['dwords_xyzw']}
                                 if stage == 'vs' else
                                 {'definition_insert_dword': profile['definition_end_dword'],
                                  'declaration_insert_dword': profile['header_end_dword'],
                                  'append_dword': end})}
    if stage == 'vs':
        output['point_and_alpha_proof'] = prove_vertex(decoded, loop)
        point_at, point_c, emissive_at, emissive_c = (428, 1, 443, 40) if loop else (389, 5, 397, 19)
        output['point_rgb_sources'] = source_role(decoded, f'c{point_c}', [point_at], loop)
        output['point_rgb_sources'][0]['color_constant_indices'] = list(range(1, 24, 3)) if loop else [5]
        output['material_emissive_scaled_sources'] = source_role(decoded, f'c{emissive_c}', [emissive_at])
        output['point_model'] = 'loop_count_i0_x_0_to_8_stride_3_a0_w' if loop else 'fixed_single_point'
        output['final_rgb_sites'] = [site(decoded, emissive_at)]
        output['alpha_output_sites'] = [site(decoded, at) for at in ((500, 505) if loop else (455, 460))]
        output['rgb_output_declaration'] = next(d for d in profile['declarations'] if d['name'] == 'o1')
        output['constraints'] = ['Convert each point RGB before its per-light multiplication; never decode the accumulated sum.',
                                 'Material emissive is already strength-scaled RGB: preserve its amplitude/tint and apply any new linear gain at this source; do not exponentiate its combined strength. Final VS RGB stays linear.',
                                 'Point and emissive consumers write XYZ only. Keep o1.w alpha/fog writes and all position/geometry instructions unchanged.']
    else:
        output['alpha_and_affine_proof'] = prove_pixel(decoded, key)
        tex, affine_end, clamp, direct, final = PIXELS[key]
        fetches = [(at, row) for at, row in decoded.items() if row['item']['opcode'] == 66]
        require(sorted(at for at, _ in fetches) == sorted(tex), 'texture source inventory changed')
        texture_roles = []
        for sampler, (at, role) in enumerate(zip(tex, ('diffuse_rgb', 'specular_data_red', 'lightmap_emissive_rgb', 'reflection_cube_rgb'))):
            row = site(decoded, at)
            require(row['sources'][1]['name'] == f's{sampler}' and row['destination']['mask'] == 'xyzw', 'texture sampler/destination changed')
            boundary = site(decoded, affine_end if sampler == 0 and affine_end else at)
            texture_roles.append({'role': role, 'sampler': sampler, 'fetch': row,
                                  'conversion_after_dword': None if sampler == 1 else boundary['end_dword'],
                                  'conversion_rgb_register': None if sampler == 1 else boundary['destination']['name'],
                                  'conversion_write_mask': None if sampler == 1 else 'xyz'})
        output['texture_sources'] = texture_roles
        output['diffuse_affine_completion'] = site(decoded, affine_end) if affine_end else None
        output['directional_rgb_sources'] = [source for c, offsets in direct.items()
                                             for source in source_role(decoded, f'c{c}', offsets)]
        output['color0_rgb_clamp'] = site(decoded, clamp)
        output['color0_declaration'] = next(d for d in profile['declarations'] if d['name'] == 'v0')
        require(output['color0_rgb_clamp']['destination']['mask'] == 'xyz' and
                'saturate' in output['color0_rgb_clamp']['destination']['modifiers'], 'COLOR0 clamp shape changed')
        output['final_rgb_sites'] = [site(decoded, final)]
        output['alpha_interpolation_site'] = site(decoded, tex[2] + 4)
        output['alpha_output_sites'] = [site(decoded, final + 4)]
        output['two_sided'] = 'vFace' in profile['declared_misc_registers']
        output['lobe_coefficients'] = FAMILIES[PIXEL_FAMILY[key]]['coefficients']
        if PIXEL_FAMILY[key] == 'shared_default':
            output['lobe_proof'] = prove_shared_lobe(decoded, key)
        output['constraints'] = ['Keep diffuse affine transform in authored space; convert only after its final RGB lane.',
                                 'All texture fetches retain original partial precision. Diffuse r1.w and lightmap r0.w feed alpha interpolation; added color writes must be XYZ only.',
                                 'The specular texture red is data, not RGB color. Directional RGB is consumed before lighting multiplication at every listed source.',
                                 'COLOR0 v0 is declared partial precision for XYZW, and final alpha uses v0.w. Removing declaration partial precision wholesale does not prove original alpha precision invariance.',
                                 'The selected plan routes full-precision RGB through VS o8/TEXCOORD6 to PS v7.xyz, preserving original partial-precision v0.w for alpha. These unused resources are proved here; no declaration or shader rewrite is emitted.',
                                 'Final output adds lighting and lightmap into oC0.xyz with partial precision. Compatibility encoding requires the complete linear sum; oC0.w must keep its independent original write.']
    require(all(row['destination']['mask'] == 'xyz' for row in output['final_rgb_sites']), 'final RGB touches alpha')
    output['certification'] = 'original_identity_and_reviewed_sites_verified; no transformed shader or numeric equivalence claim'
    return output


def prove_archive_coverage(inventory):
    """Each named alias must independently cover its complete ten-pair family."""
    for family in FAMILIES.values():
        expected = {(vs, ps) for vs, ps in PAIRS if ps in family['pixels']}
        require(len(expected) == 10, 'family pair contract changed')
        for alias in family['aliases']:
            basename = re.compile(re.escape(alias) + r'(?:2s|_000[01])?')
            actual = {(row['vs'], row['ps']) for row in inventory['pairs']
                      if 'DEFAULT' in row['effects']['techniques'] and '3_0' in row['effects']['profile_directories']
                      and any(basename.fullmatch(name) for name in row['effects']['basenames'])}
            require(actual == expected, f'{alias}: complete DEFAULT archive coverage changed')


def inspect(directory, inventory_path):
    inventory_data = inventory_path.read_bytes()
    inventory = json.loads(inventory_data)
    prove_archive_coverage(inventory)
    rows = [row for row in inventory['pairs'] if (row['vs'], row['ps']) in PAIRS]
    negative = [row for row in inventory['pairs'] if row['vs'] == BASE_VS and row['ps'] == '462342e3e5781384']
    require(len(negative) == 1 and negative[0]['transformation_class'] == 'A_reference_registers' and
            (BASE_VS, '462342e3e5781384') not in PAIRS, 'future negative witness changed')
    require(len(rows) == 20 and {(row['vs'], row['ps']) for row in rows} == PAIRS, 'missing/duplicate reviewed pair')
    require(all(row['transformation_class'] == 'A_reference_registers' for row in rows), 'motion class changed')
    depth = motion.depth_plan(inventory)
    codes = {key: (directory / f'{key}.bin').read_bytes() for key in ORIGINALS}
    programs = [inspect_program(code, key) for key, code in codes.items()]
    original_motion = {key: motion.profile(code, key, key[:2], '3_0') for key, code in codes.items()}
    compact_pairs = []
    for row in sorted(rows, key=lambda row: (row['vs'], row['ps'])):
        plan, d = row['insertion_plan'], depth[row['vs'], row['ps']]
        actual = motion.classify(original_motion['vs_' + row['vs']], original_motion['ps_' + row['ps']])
        require(actual[0] == 'A_reference_registers' and not actual[1] and actual[3] == plan, 'motion source/splice plan changed')
        require((plan['vs_output_register'], plan['ps_input_register'], plan['ps_temporaries'],
                 plan['texcoord_index'], plan['vs_constant_base'], plan['ps_constant_base']) ==
                (6, 5, [5, 6, 7], 4, 252, 216), 'motion ABI changed')
        require(d == {'vertex_depth_output_register': 7, 'depth_texcoord_index': 5,
                      'pixel_depth_input_register': 6, 'depth_output': True}, 'depth ABI changed')
        for stage in ('vs', 'ps'):
            name = stage + '_' + row[stage]
            original = next(p for p in programs if p['id'] == name)
            require(inventory['programs'][name]['sha256'] == original['sha256'], 'stale motion program identity')
        compact_pairs.append({'vs': row['vs'], 'ps': row['ps'], 'family': PIXEL_FAMILY[row['ps']],
                              'archive_pass_occurrences': row['effects']['pass_occurrences'],
                              'archive_aliases': row['effects']['basenames'],
                              'motion_class': 'A', 'current_depth_supported': True})
    stage_constraints = {}
    for program in programs:
        stage = program['id'][:2]
        constraints = program.pop('constraints')
        require(stage not in stage_constraints or stage_constraints[stage] == constraints, 'inconsistent stage constraints')
        stage_constraints[stage] = constraints
        program['constraints_ref'] = stage
    return {'schema': 1, 'stage_constraints': stage_constraints, 'scope': 'Bounded DEFAULT original-site proof: 3 shared VS, 12 PS, 20 archive pairs; six shared-default PS remain offline-only.',
            'families': FAMILIES,
            'pending_production_requirements': ['Extend only the six shared-default PS and ten explicit pairs after review; current production remains Argon-only.',
                                                'New e3b7acc16da9932d is 1296 DWORDs, beyond current production 1292 guard. Requalify the bounded guard when extending.',
                                                'Retain shared VS variant consistency and the existing o8/v7/TEXCOORD6 ABI; no varying allocator is introduced.'],
            'future_negative_pair': {'vs': BASE_VS, 'ps': '462342e3e5781384', 'family': 'split', 'motion_class': 'A',
                                     'note': 'Still uncovered after this pending slice; existing live fixture is unchanged.'},
            'offset_units': 'Zero-based DWORD positions in the ORIGINAL whole program, including opaque comments; end_dword/conversion_after_dword are exclusive.',
            'motion_inventory_sha256': hashlib.sha256(inventory_data).hexdigest(),
            'reserved_abi': {'vs_constants': [252, 255], 'vs_motion_output': 6, 'vs_depth_output': 7,
                             'ps_constants': [216, 220], 'ps_temporaries': [5, 7], 'ps_motion_input': 5,
                             'ps_depth_input': 6, 'ps_motion_output': 1, 'ps_depth_output': 2,
                             'motion_texcoord': 4, 'depth_texcoord': 5,
                             'material_vs_def_constants': [248, 249], 'material_ps_def_constants': [212, 213],
                             'material_vs_rgb_output': 8, 'material_ps_rgb_input': 7,
                             'material_rgb_mask': 'xyz', 'material_rgb_texcoord': 6,
                             'material_rgb_precision': 'full'},
            'precision_note': 'Instruction destination modifiers and declaration modifiers contain partial_precision; source_modifier is the D3DSHADER_PARAM_SRCMOD_TYPE numeric field. Source operands have no independent partial-precision flag.',
            'limits': ['Identity binds all definitions, comments/preshaders and END. Only actual instructions are decoded.',
                       'Free resources exclude current motion, current-depth and selected material RGB/DEF reservations, including depth-off variants.',
                       'Relative VS constant availability assumes the existing runtime i0 light-count guard [0,8].',
                       'This artifact proves original sites only. Argon runtime evidence remains its existing qualified checkpoint; the shared-default extension has no production/GPU/native-Windows qualification.'],
            'pairs': compact_pairs, 'programs': programs}


def write_report(path, result):
    header = {key: value for key, value in result.items() if key not in ('pairs', 'programs')}
    text = json.dumps(header, indent=2)[:-2]
    for name in ('pairs', 'programs'):
        text += ',\n  ' + json.dumps(name) + ': [\n'
        text += ',\n'.join('    ' + json.dumps(row, separators=(',', ':')) for row in result[name])
        text += '\n  ]'
    path.write_text(text + '\n}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program_directory', type=Path)
    parser.add_argument('--motion-inventory', type=Path, default=Path('verification/results/motion-output-profiles.json'))
    parser.add_argument('--json', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.program_directory, args.motion_inventory)
    write_report(args.json, result)
    print(f"certified {len(result['programs'])} originals and {len(result['pairs'])} pairs; no shader transformation")


if __name__ == '__main__':
    main()
