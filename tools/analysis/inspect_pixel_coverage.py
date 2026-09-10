#!/usr/bin/env python3
"""Qualify exact local PS programs for a coverage-only replacement whitelist.

This is an offline gate, not a shader validator or shading-equivalence proof.
``inspect(code, features)`` requires explicit semantic inventory evidence and
independently walks complete raw token boundaries. Comments and DEF literals are
never interpreted as instructions. Legacy texture instructions remain eligible.
Unknown framing/opcodes/destinations fail closed. Predicate and relative-address
forms are deliberately unsupported by this bounded gate.

A positive result proves only that this exact program has no shader discard or
shader depth write. Alpha test, stencil, clipping, rasterization, blend state,
multisampling, render targets and vertex shader compatibility remain caller gates.
No shader bytes, literal values, or disassembly are returned.
"""
from collections import Counter
import struct


# D3DSHADER_INSTRUCTION_OPCODE_TYPE from the local D3D9 SDK definitions.
_NAMES = (
    'nop mov add sub mad mul rcp rsq dp3 dp4 min max slt sge exp log lit dst lrp frc '
    'm4x4 m4x3 m3x4 m3x3 m3x2 call callnz loop ret endloop label dcl pow crs sgn '
    'abs nrm sincos rep endrep if ifc else endif break breakc mova defb defi'
).split()
_OPCODES = dict(enumerate(_NAMES)) | dict(zip(range(64, 97), (
    'texcoord texkill tex texbem texbeml texreg2ar texreg2gb texm3x2pad texm3x2tex '
    'texm3x3pad texm3x3tex texm3x3diff texm3x3spec texm3x3vspec expp logp cnd def '
    'texreg2rgb texdp3tex texm3x2depth texdp3 texm3x3 texdepth cmp bem dp2add dsx dsy '
    'texldd setp texldl breakp'
).split())) | {0xfffd: 'phase'}
_TEXT_OPCODE = {name: number for number, name in _OPCODES.items()} | {
    'texld': 66, 'texldp': 66, 'texldb': 66, 'texcrd': 64}
# Operand counts before optional relative-address/predicate tokens. Those forms
# are rejected rather than guessed. DCL includes its usage word; DEF includes
# literal words. SM1 TEX/TEXCOORD and SM3 SINCOS are adjusted below.
_ARITY = dict(zip(range(49), (
    0, 2, 3, 3, 4, 3, 2, 2, 3, 3, 3, 3, 3, 3, 2, 2, 2, 3, 4, 2,
    3, 3, 3, 3, 3, 1, 2, 2, 0, 0, 1, 2, 3, 3, 4, 2, 2, 4,
    1, 0, 1, 2, 0, 0, 0, 2, 2, 2, 5
))) | dict(zip(range(64, 97), (
    1, 1, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 4, 5,
    2, 2, 2, 2, 2, 1, 4, 3, 4, 2, 2, 5, 3, 3, 1
))) | {0xfffd: 0}
_NO_DEST = {0, 25, 26, 27, 28, 29, 30, 38, 39, 40, 41, 42, 43, 44, 45, 96, 0xfffd}
_DEFINITIONS = {47: 14, 48: 7, 81: 2}
_ALLOWED_INVENTORY_UNKNOWNS = {'control_flow', 'coissued_instruction', 'unsupported_instruction'}


def _register_type(token):
    return ((token >> 28) & 7) | ((token >> 8) & 24)


def inspect(code: bytes, features: dict) -> dict:
    """Return ``qualified`` and stable ``reason`` plus minimal derived metadata.

    Qualification binds to the caller's exact bytecode hash; callers must retain
    that identity in generated registries and never use this result for another
    program. Inventory must be generated from the same program, and its complete
    opcode histogram/instruction count is cross-checked against raw boundaries.
    """
    result = {'qualified': False, 'reason': 'missing_inventory_evidence'}

    def fail(reason, index=None):
        result['reason'] = reason
        if index is not None:
            result['instruction_index'] = index
        return result

    if not isinstance(features, dict):
        return result
    required = {'stage', 'model', 'instruction_count', 'opcode_counts', 'unknown_reasons',
                'texkill_instruction_indices', 'depth_write_instruction_indices'}
    if not required <= features.keys():
        return result
    if not isinstance(features['stage'], str) or not isinstance(features['model'], str):
        return fail('malformed_inventory_evidence')
    for field in ('unknown_reasons', 'texkill_instruction_indices', 'depth_write_instruction_indices'):
        if not isinstance(features[field], list):
            return fail('malformed_inventory_evidence')
    if not all(isinstance(value, str) for value in features['unknown_reasons']):
        return fail('malformed_inventory_evidence')
    if any(type(value) is not int or value < 0 for field in
           ('texkill_instruction_indices', 'depth_write_instruction_indices') for value in features[field]):
        return fail('malformed_inventory_evidence')
    if (type(features['instruction_count']) is not int or features['instruction_count'] <= 0
            or not isinstance(features['opcode_counts'], dict) or not features['opcode_counts']):
        return fail('malformed_inventory_evidence')
    if any(not isinstance(name, str) or type(count) is not int or count <= 0
           for name, count in features['opcode_counts'].items()):
        return fail('malformed_inventory_evidence')
    if features['stage'] != 'ps':
        return fail('not_pixel_shader_inventory')
    if features['texkill_instruction_indices']:
        return fail('inventory_reports_discard')
    if features['depth_write_instruction_indices']:
        return fail('inventory_reports_depth_write')
    if set(features['unknown_reasons']) - _ALLOWED_INVENTORY_UNKNOWNS:
        return fail('unresolved_inventory_syntax')
    if not isinstance(code, bytes) or len(code) < 8 or len(code) % 4:
        return fail('malformed_bytecode_size')
    words = struct.unpack('<%dI' % (len(code) // 4), code)
    major, minor = (words[0] >> 8) & 255, words[0] & 255
    if words[0] >> 16 != 0xffff or (major, minor) not in {
            (1, 1), (1, 2), (1, 3), (1, 4), (2, 0), (2, 1), (3, 0)}:
        return fail('unsupported_pixel_shader_version')
    result['shader_model'] = f'{major}_{minor}'
    if features['model'] not in ({'2_x', '2_a', '2_b', '2_1'} if (major, minor) == (2, 1)
                                 else {result['shader_model']}):
        return fail('inventory_version_mismatch')
    expected_counts = Counter()
    for name, count in features['opcode_counts'].items():
        if name not in _TEXT_OPCODE:
            return fail('unknown_inventory_opcode')
        expected_counts[_TEXT_OPCODE[name]] += count

    position, instruction_index, actual_counts = 1, 0, Counter()
    while position < len(words):
        header = words[position]
        opcode = header & 0xffff
        if header == 0xffff:
            if position != len(words) - 1:
                return fail('trailing_bytecode')
            break
        if header & 0x80000000:
            return fail('malformed_instruction_header', instruction_index)
        if opcode == 0xfffe:
            comment_words = (header >> 16) & 0x7fff
            position += 1 + comment_words
            if position >= len(words):
                return fail('truncated_comment_or_missing_end')
            continue
        if opcode not in _ARITY:
            return fail('unknown_raw_opcode', instruction_index)
        if opcode == 65:
            return fail('raw_shader_discard', instruction_index)
        if opcode in {84, 87}:
            return fail('raw_shader_depth_write', instruction_index)
        # Relative/predicated encodings are outside this small proof, not assumed
        # to be ordinary operands. Coissue is safe for SM1 coverage inspection.
        if header & 0x30000000 or (header & 0x40000000 and major != 1):
            return fail('unsupported_instruction_flags', instruction_index)
        controls = (header >> 16) & 255
        if controls and not ((opcode == 66 and major >= 2 and controls in {1, 2}) or
                             (opcode in {41, 45, 94} and controls in range(1, 7))):
            return fail('unsupported_instruction_controls', instruction_index)
        count = _ARITY[opcode]
        if opcode in {64, 66} and major == 1:
            count = 2 if minor == 4 else 1
        if opcode == 37 and major == 3:
            count = 2
        if (major >= 2 and ((header >> 24) & 15) != count) or (major == 1 and header & 0x0f000000):
            return fail('instruction_length_mismatch', instruction_index)
        if opcode == 0xfffd and (major, minor) != (1, 4):
            return fail('unsupported_phase', instruction_index)
        if position + count >= len(words):
            return fail('truncated_instruction', instruction_index)
        operands = words[position + 1:position + 1 + count]
        destination_index = 1 if opcode == 31 else 0
        if opcode not in _NO_DEST:
            destination = operands[destination_index]
            destination_type = _register_type(destination)
            if destination_type == 9:
                return fail('raw_depth_output_destination', instruction_index)
            allowed_types = ({_DEFINITIONS[opcode]} if opcode in _DEFINITIONS else
                             {1, 3, 10, 17} if opcode == 31 else
                             {19} if opcode == 94 else
                             {0, 3} if major == 1 else {0, 8, 16})
            if (not destination & 0x80000000 or destination_type not in allowed_types or
                    destination & 0x00002000 or not destination & 0x000f0000):
                return fail('malformed_or_unsupported_destination', instruction_index)
        # Definition literal words and DCL usage words are data, not source
        # registers. Every other operand must be a register token; reject rather
        # than swallow instruction-looking words through a dishonest length.
        for operand_index, operand in enumerate(operands):
            if opcode in _DEFINITIONS and operand_index > 0:
                continue
            if opcode == 31 and operand_index == 0:
                continue
            if not operand & 0x80000000 or _register_type(operand) > 19:
                return fail('malformed_register_operand', instruction_index)
            if operand & 0x00002000:
                return fail('unsupported_relative_addressing', instruction_index)
        actual_counts[opcode] += 1
        instruction_index += 1
        position += 1 + count
    else:
        return fail('missing_end')
    result['instruction_count'] = instruction_index
    if instruction_index != features['instruction_count'] or actual_counts != expected_counts:
        return fail('inventory_opcode_mismatch')
    result.update(qualified=True, reason='no_shader_discard_or_depth_write')
    return result
