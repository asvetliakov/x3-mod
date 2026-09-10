#!/usr/bin/env python3
"""Classify every archived VS position path with bounded token-level proofs.

The existing captured-rigid report/production registry are not changed. Additional
proofs cover direct clip-position and the observed view-space billboard shape.
Unsupported shapes remain explicit unknowns. Output contains only metadata.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path

from inspect_rigid_positions import destination, instructions, prove, reg, source


def classify(code):
    rigid = prove(code)
    if rigid['qualified_position_math']:
        return {'category': 'homogeneous_row_dots', 'proof': rigid}
    words, ops = instructions(code)
    base = {k: rigid[k] for k in ('version', 'bytes', 'word_count', 'fnv1a64', 'sha256')}

    def unknown(reason):
        return {'category': 'unknown', 'proof': {**base, 'reason': reason,
                                               'row_dot_rejection': rigid['reason']}}

    # These exception proofs deliberately support only the straight-line opcode
    # subset actually present in the remaining archive programs. No unknown flow
    # or side effects are ignored to obtain a more attractive classification.
    if any(op['opcode'] not in {1, 2, 4, 5, 9, 31, 81}
           or op['token'] & 0x50000000 for op in ops):
        return unknown('exception_shape_has_unsupported_instruction_or_flow')
    declarations, literal_defs = [], {}
    for op in ops:
        args = op['args']
        if op['opcode'] == 31 and len(args) == 2:
            declarations.append({'kind': reg(args[1])[0], 'register': reg(args[1])[1],
                'usage': args[0] & 15, 'usage_index': (args[0] >> 16) & 15,
                'mask': (args[1] >> 16) & 15, 'instruction_dword': op['offset']})
        elif op['opcode'] == 81:
            if len(args) != 5 or reg(args[0])[0] != 2 or reg(args[0])[1] in literal_defs:
                return unknown('unsupported_or_duplicate_literal_definition')
            literal_defs[reg(args[0])[1]] = op
    pos_inputs = [d for d in declarations if d['kind'] == 1 and d['usage'] == 0 and d['usage_index'] == 0]
    if len(pos_inputs) != 1 or pos_inputs[0]['mask'] != 15:
        return unknown('POSITION0_input_declaration_not_full_unique')
    input_register = pos_inputs[0]['register']
    if (words[0] >> 8) & 255 < 3:
        output = (4, 0)
    else:
        outputs = [d for d in declarations if d['kind'] == 6 and d['usage'] == 0 and d['usage_index'] == 0]
        if len(outputs) != 1 or outputs[0]['mask'] != 15:
            return unknown('position_output_declaration_not_full_unique')
        output = (6, outputs[0]['register'])
    writes = [op for op in ops if op['opcode'] not in (31, 81) and op['args']]
    position_writes = [op for op in writes if reg(op['args'][0]) == output]
    common = {**base, 'declared_inputs': [d for d in declarations if d['kind'] == 1],
              'input_register': input_register, 'output_register_type': output[0],
              'output_register': output[1], 'conditional_position_writes': False}

    def homogeneous_constructor(op, target):
        args = op['args']
        if op['opcode'] != 4 or len(args) != 4 or args[0] != destination(*target):
            return None
        if args[1] != source(1, input_register, 0x24) or reg(args[2])[0] != 2:
            return None
        literal = reg(args[2])[1]
        if args[2:] != [source(2, literal, 0x40), source(2, literal, 0x15)]:
            return None
        definition = literal_defs.get(literal)
        if definition is None or definition['args'][1:3] != [0x3f800000, 0]:
            return None
        return {'constructor_dword': op['offset'], 'literal_register': literal,
                'literal_def_dword': definition['offset']}

    if len(position_writes) == 1:
        op, = position_writes
        if op['opcode'] == 1 and op['args'] == [destination(*output), source(1, input_register)]:
            return {'category': 'direct_clip_xyzw', 'proof': {**common,
                    'input_w_used': True, 'position_write_dword': op['offset'],
                    'contract': 'ClipXYZWEqualsConvertedPOSITION0'}}
        constructor = homogeneous_constructor(op, output)
        if constructor:
            return {'category': 'direct_clip_xyz_w_one', 'proof': {**common, **constructor,
                    'input_w_used': False, 'contract': 'ClipXYZEqualsConvertedPOSITION0_WOneFiniteAlgebra'}}
        return unknown('single_position_write_not_reviewed_direct_shape')

    def row_dots(group, target):
        if len(group) != 4:
            return None
        rows, temporary = {}, None
        for op in group:
            args = op['args']
            mask = (args[0] >> 16) & 15
            if (op['opcode'] != 9 or len(args) != 3 or mask not in (1, 2, 4, 8)
                    or mask in rows or args[0] != destination(*target, mask)
                    or reg(args[1])[0] != 0 or args[1] != source(*reg(args[1]))
                    or reg(args[2])[0] != 2 or args[2] != source(*reg(args[2]))):
                return None
            if temporary is not None and temporary != reg(args[1])[1]:
                return None
            temporary = reg(args[1])[1]
            rows[mask] = (reg(args[2])[1], op['offset'])
        first = rows[1][0]
        if [rows[m][0] for m in (1, 2, 4, 8)] != list(range(first, first + 4)):
            return None
        if any(row in literal_defs for row in range(first, first + 4)):
            return None
        return {'matrix_first_register': first, 'source_temp': temporary,
                'dot_dwords_xyzw': [rows[m][1] for m in (1, 2, 4, 8)]}

    projection = row_dots(position_writes, output)
    if projection is None:
        return unknown('position_not_reviewed_projection_row_dots')
    transformed = projection['source_temp']
    first_projection = min(projection['dot_dwords_xyzw'])
    last_projection = max(projection['dot_dwords_xyzw'])
    transformed_writes = [op for op in writes if reg(op['args'][0]) == (0, transformed)
                          and op['offset'] <= last_projection]
    view_dots = [op for op in transformed_writes if op['opcode'] == 9]
    additions = [op for op in transformed_writes if op['opcode'] == 2]
    view = row_dots(view_dots, (0, transformed))
    if view is None or len(transformed_writes) != 5 or len(additions) != 1:
        return unknown('intermediate_not_four_view_dots_plus_one_add')
    addition, = additions
    offset_inputs = [d for d in declarations if d['kind'] == 1 and d['usage'] == 5
                     and d['usage_index'] == 0 and d['mask'] & 3 == 3]
    if len(offset_inputs) != 1:
        return unknown('billboard_offset_not_unique_TEXCOORD0_xy')
    offset_register = offset_inputs[0]['register']
    if addition['args'] != [destination(0, transformed, 3), source(0, transformed), source(1, offset_register)]:
        return unknown('intermediate_add_not_plain_XY_offset')
    if (addition['offset'] <= max(view['dot_dwords_xyzw'][:2])
            or max(op['offset'] for op in transformed_writes) >= first_projection):
        return unknown('billboard_offset_or_view_dot_liveness_order_wrong')
    homogeneous = view['source_temp']
    last_view = max(view['dot_dwords_xyzw'])
    homogeneous_writes = [op for op in writes if reg(op['args'][0]) == (0, homogeneous)
                          and op['offset'] <= last_view]
    if len(homogeneous_writes) != 1 or homogeneous_writes[0]['offset'] >= min(view['dot_dwords_xyzw']):
        return unknown('billboard_center_temp_not_single_prior_definition')
    constructor = homogeneous_constructor(homogeneous_writes[0], (0, homogeneous))
    if constructor is None:
        return unknown('billboard_center_not_homogeneous_POSITION0')
    return {'category': 'view_xy_billboard_projection', 'proof': {**common, **constructor,
            'input_w_used': False, 'offset_input_register': offset_register,
            'offset_usage': 'TEXCOORD0.xy', 'offset_add_dword': addition['offset'],
            'view': view, 'projection': projection,
            'contract': 'ProjectionRowsDot(ViewRowsDot(XYZ_One)+TEXCOORD0_XY_ZeroZW)',
            'finite_input_algebra_only': True}}


def inspect(inventory_path, aliases_path, raw_directory):
    inventory_bytes, aliases_bytes = inventory_path.read_bytes(), aliases_path.read_bytes()
    inventory, aliases = json.loads(inventory_bytes), json.loads(aliases_bytes)
    families = defaultdict(set)
    for effect in aliases['effects']:
        for program in effect['program_occurrences']:
            families[program].add(Path(effect['path']).stem)
    programs = []
    for program in inventory['programs']:
        if program['stage'] != 'vs':
            continue
        code = (raw_directory / (program['id'] + '.bin')).read_bytes()
        if hashlib.sha256(code).hexdigest() != program['sha256']:
            raise ValueError('Program differs from complete sweep')
        result = classify(code)
        programs.append({'id': program['id'], 'model': program['model'],
                         'runtime_captured': program['runtime_captured'],
                         'basename_aliases': sorted(families[program['id']]), **result})
    return {'schema': 1, 'scope': 'Every VS in the installed archive sweep; structural classification only, no registry expansion',
            'inventory_sha256': hashlib.sha256(inventory_bytes).hexdigest(),
            'aliases_sha256': hashlib.sha256(aliases_bytes).hexdigest(),
            'tool_source_sha256': {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                  for name in ('inspect_archive_positions.py', 'inspect_rigid_positions.py', 'index_shaders.py', 'shader_constants.py')},
            'program_count': len(programs), 'category_counts': dict(sorted(Counter(p['category'] for p in programs).items())),
            'captured_program_count': sum(p['runtime_captured'] for p in programs),
            'captured_category_counts': dict(sorted(Counter(p['category'] for p in programs if p['runtime_captured']).items())),
            'unknown_programs': [p['id'] for p in programs if p['category'] == 'unknown'], 'programs': programs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--aliases', type=Path, required=True)
    parser.add_argument('--raw-directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.inventory, args.aliases, args.raw_directory)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('program_count', 'category_counts', 'captured_program_count', 'captured_category_counts', 'unknown_programs')}))


if __name__ == '__main__':
    main()
