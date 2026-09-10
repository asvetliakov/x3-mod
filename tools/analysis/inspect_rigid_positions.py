#!/usr/bin/env python3
"""Prove a narrow raw-XYZ/W=1 position contract in captured D3D9 shaders.

This reads local bytecode but exports only hashes, derived register/offset facts
and declaration coverage. It is not a generic equivalence prover or eligibility
decision: object identity, unchanged vertex content, scene state and raster parity
are separate runtime gates. Unknown shapes fail closed.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct

from index_shaders import fnv1a64, shader_end, SM1_ARITY
from shader_constants import parse_ctab


def reg(token):
    return ((token >> 28) & 7) | ((token >> 8) & 24), token & 2047


def source(kind, number, swizzle=0xe4):
    return 0x80000000 | ((kind & 7) << 28) | ((kind & 24) << 8) | (swizzle << 16) | number


def destination(kind, number, mask=15):
    return 0x80000000 | ((kind & 7) << 28) | ((kind & 24) << 8) | (mask << 16) | number


def instructions(code):
    if len(code) % 4:
        raise ValueError('unaligned bytecode')
    words = struct.unpack(f'<{len(code)//4}I', code)
    if not words or words[0] >> 16 != 0xfffe or shader_end(words, 0) != len(words):
        raise ValueError('not one complete vertex shader')
    major, minor = (words[0] >> 8) & 255, words[0] & 255
    result, pos = [], 1
    while pos < len(words) - 1:
        token, opcode = words[pos], words[pos] & 65535
        count = ((token >> 16) & 32767) if opcode == 0xfffe else (
            SM1_ARITY[opcode] if major == 1 else (token >> 24) & 15)
        if major == 1 and minor == 4 and opcode in (64, 66):
            count = 2
        if opcode != 0xfffe:
            result.append({'offset': pos, 'token': token, 'opcode': opcode,
                           'args': list(words[pos + 1:pos + 1 + count])})
        pos += 1 + count
    return words, result


def prove(code):
    """Accept only the exact homogeneous MAD + four independent row DP4 shape."""
    words, ops = instructions(code)
    major = (words[0] >> 8) & 255
    result = {'qualified_position_math': False, 'version': f'0x{words[0]:08x}',
              'bytes': len(code), 'word_count': len(words), 'fnv1a64': fnv1a64(code),
              'sha256': hashlib.sha256(code).hexdigest()}

    def reject(reason):
        return {**result, 'reason': reason}

    inputs, position_outputs, definitions = [], [], {}
    for op in ops:
        args = op['args']
        if op['opcode'] == 31 and len(args) == 2:
            kind, number = reg(args[1])
            usage, index = args[0] & 15, (args[0] >> 16) & 15
            if kind == 1:
                inputs.append({'register': number, 'usage': usage, 'usage_index': index,
                               'mask': (args[1] >> 16) & 15, 'declaration_dword': op['offset']})
            elif kind == 6 and usage == 0 and index == 0:
                position_outputs.append((kind, number))
        elif op['opcode'] == 81 and len(args) == 5 and reg(args[0])[0] == 2:
            if reg(args[0])[1] in definitions:
                return reject('duplicate_literal_definition')
            definitions[reg(args[0])[1]] = op
    result['declared_inputs'] = inputs
    position_inputs = [item for item in inputs if item['usage'] == 0 and item['usage_index'] == 0]
    if len(position_inputs) != 1:
        return reject('not_exactly_one_declared_POSITION0_input')
    input_register = position_inputs[0]['register']
    if major < 3:
        position_outputs = [(4, 0)]  # oPos, fixed VS1/2 output register.
    if len(position_outputs) != 1:
        return reject('not_exactly_one_position_output')
    output = position_outputs[0]

    # Structured flow elsewhere is allowed only if all position writes and the
    # constructor are outside it. Conservatively reject all other flow forms.
    stack = []
    nonwriters = {0, 31, 38, 39, 40, 41, 42, 43, 81}
    writes = []
    for op in ops:
        opcode = op['opcode']
        op['depth'] = len(stack)
        if opcode in {25, 26, 27, 28, 29, 30, 44, 45, 96}:
            return reject('unsupported_control_flow')
        if op['token'] & 0x50000000:
            return reject('predicated_or_coissued_instruction')
        if opcode in (38, 40, 41):
            stack.append('rep' if opcode == 38 else 'if')
        elif opcode == 42:
            if not stack or stack[-1] != 'if':
                return reject('malformed_else')
        elif opcode in (39, 43):
            expected = 'rep' if opcode == 39 else 'if'
            if not stack or stack.pop() != expected:
                return reject('malformed_end')
        if opcode not in nonwriters and op['args']:
            writes.append(op)
    if stack:
        return reject('unclosed_control_flow')
    output_writes = [op for op in writes if reg(op['args'][0]) == output]
    if len(output_writes) != 4:
        return reject('position_is_not_four_row_dots')
    rows = {}
    temp = None
    for op in output_writes:
        args = op['args']
        mask = (args[0] >> 16) & 15
        if (op['opcode'] != 9 or len(args) != 3 or mask not in (1, 2, 4, 8)
                or args[0] != destination(*output, mask) or op['depth'] != 0):
            return reject('position_write_not_unmodified_unconditional_DP4')
        if mask in rows:
            return reject('duplicate_position_lane')
        if reg(args[1])[0] != 0 or args[1] != source(*reg(args[1])):
            return reject('position_dot_source_not_plain_temp')
        if temp is not None and temp != reg(args[1])[1]:
            return reject('different_position_source_temps')
        temp = reg(args[1])[1]
        if reg(args[2])[0] != 2 or args[2] != source(*reg(args[2])):
            return reject('position_dot_row_not_direct_plain_constant')
        rows[mask] = (reg(args[2])[1], op['offset'])
    base = rows[1][0]
    if [rows[mask][0] for mask in (1, 2, 4, 8)] != list(range(base, base + 4)):
        return reject('position_rows_not_consecutive_XYZW')
    if any(row in definitions for row in range(base, base + 4)):
        return reject('position_matrix_overlaps_shader_literal')
    last_dot = max(offset for _, offset in rows.values())
    temp_writes = [op for op in writes if reg(op['args'][0]) == (0, temp) and op['offset'] <= last_dot]
    if len(temp_writes) != 1:
        return reject('position_temp_not_single_definition_through_final_use')
    constructor = temp_writes[0]
    args = constructor['args']
    if (constructor['opcode'] != 4 or len(args) != 4 or constructor['depth'] != 0
            or constructor['offset'] >= min(offset for _, offset in rows.values())
            or args[0] != destination(0, temp)
            or args[1] != source(1, input_register, 0x24) or reg(args[2])[0] != 2
            or reg(args[3]) != reg(args[2])):
        return reject('position_temp_not_homogeneous_input_MAD')
    literal = reg(args[2])[1]
    if args[2:] != [source(2, literal, 0x40), source(2, literal, 0x15)]:
        return reject('constructor_literal_swizzles_not_XYZ_one_pattern')
    definition = definitions.get(literal)
    if not definition or definition['args'][1:3] != [0x3f800000, 0]:
        return reject('constructor_not_shader_local_one_zero')
    # This list exposes potential indirect aliases instead of pretending a base
    # register check establishes an upper bound for relative addressing.
    direct_row_other_uses, relative_constants = [], []
    dot_offsets = {offset for _, offset in rows.values()}
    for op in ops:
        if op['opcode'] in (31, 81):
            continue
        operands = op['args'] if op['opcode'] in nonwriters else op['args'][1:]
        for token in operands:
            if reg(token)[0] == 2:
                if token & 0x2000:
                    relative_constants.append({'instruction_dword': op['offset'], 'base_register': reg(token)[1]})
                elif base <= reg(token)[1] < base + 4 and op['offset'] not in dot_offsets:
                    direct_row_other_uses.append(op['offset'])
    parameters = parse_ctab(code)
    matrix_names = [p['name'] for p in parameters if p['register_set'] == 2
                    and p['register'] == base and p['count'] == 4]
    return {**result, 'qualified_position_math': True,
        'contract': 'FloatXYZForceWOneSubmittedRowDots', 'input_register': input_register,
        'input_w_used': False, 'output_register_type': output[0], 'output_register': output[1],
        'matrix_first_register': base, 'matrix_names_only': matrix_names,
        'position_dot_dwords_xyzw': [rows[m][1] for m in (1, 2, 4, 8)],
        'constructor_dword': constructor['offset'], 'constructor_temp_register': temp,
        'literal_def_dword': definition['offset'], 'literal_register': literal,
        'direct_matrix_nonposition_uses': sorted(set(direct_row_other_uses)),
        'relative_constant_reads': relative_constants,
        'position_modifiers': [], 'conditional_position_writes': False,
        'scope': 'Algebraic contract for ordinary finite inputs; not bitwise behavior for NaN/Inf/subnormals or a temporal-rigidity proof'}


def declaration_coverage(path):
    counts, current = defaultdict(Counter), None
    streams, light_counts = defaultdict(Counter), defaultdict(Counter)
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for raw in stream:
            digest.update(raw)
            if raw.startswith(b'draw '):
                match = re.search(rb' vs=([0-9a-f]{16})', raw)
                current = match[1].decode() if match else None
            elif current and raw.startswith(b'vertex_element '):
                item = {k.decode(): int(v) for k, v in re.findall(rb'(\w+)=(\d+)', raw)}
                if item.get('usage') == 0 and item.get('index') == 0 and item.get('stream') != 255:
                    counts[current][tuple(item[k] for k in ('stream', 'offset', 'type', 'method'))] += 1
            elif current and raw.startswith(b'stream slot=0 result=00000000 '):
                item = {k.decode(): int(v) for k, v in re.findall(rb'(\w+)=(\d+)', raw)}
                if item.get('identity') and all(k in item for k in ('offset', 'stride', 'frequency')):
                    streams[current][tuple(item[k] for k in ('offset', 'stride', 'frequency'))] += 1
            elif current and raw.startswith(b'constant kind=vs type=i reg=0 values='):
                light_counts[current][int(raw.split(b'values=', 1)[1].split(b',', 1)[0])] += 1
    return {'source_basename': path.name, 'source_sha256': digest.hexdigest(),
            'scope': 'Recorded POSITION0 declaration occurrences; not successful draw/pass eligibility',
            'programs': {key: [dict(zip(('stream', 'offset', 'type', 'method'), element), count=count)
                               for element, count in sorted(values.items())] for key, values in sorted(counts.items())},
            'stream0_bindings': {key: [dict(zip(('offset', 'stride', 'frequency'), element), count=count)
                               for element, count in sorted(values.items())] for key, values in sorted(streams.items())},
            'integer_i0_x': {key: dict(sorted(values.items())) for key, values in sorted(light_counts.items())}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--raw-directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--capture-log', type=Path)
    args = parser.parse_args()
    inventory_bytes = args.inventory.read_bytes()
    inventory = json.loads(inventory_bytes)
    profiles = []
    for program in inventory['programs']:
        if program['stage'] != 'vs' or not program['runtime_captured']:
            continue
        code = (args.raw_directory / (program['id'] + '.bin')).read_bytes()
        if hashlib.sha256(code).hexdigest() != program['sha256']:
            raise ValueError('Program no longer matches reviewed sweep')
        profiles.append(prove(code))
    result = {'schema': 1, 'inventory_sha256': hashlib.sha256(inventory_bytes).hexdigest(),
              'inspector_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'scope': 'Captured VS subset of complete archive inventory; structural position proof only',
              'program_count': len(profiles), 'qualified_count': sum(p['qualified_position_math'] for p in profiles),
              'programs': profiles}
    if args.capture_log:
        result['declaration_coverage'] = declaration_coverage(args.capture_log)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('program_count', 'qualified_count')}))


if __name__ == '__main__':
    main()
