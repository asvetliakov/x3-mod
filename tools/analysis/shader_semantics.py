#!/usr/bin/env python3
"""Conservative derived inventory of local D3DXDisassembleShader text.

``analyze(text, ctab=None)`` accepts the list returned by ``parse_ctab`` (or a
dictionary containing that list at ``constants``). It returns JSON-safe counts,
semantic declarations, register/parameter references and output dependencies;
it does not retain raw instructions or literal constant values. Instruction
indices are zero-based ordinals including declarations/definitions, NOT bytecode
DWORD offsets. No runtime state or physical meaning is inferred from names.

The dependency interpreter handles common SM1-3 arithmetic conservatively, with
component tracking for ordinary arithmetic and dot/matrix operations. A complete
result is an overapproximation of *possible dependencies*, not algebraic or
numeric equivalence. It does not simplify multiplication by zero. Control flow,
coissue/predication, malformed syntax, and unsupported operations invalidate all
output proofs. Relative constant indexing invalidates each dependent output.
Partial writes preserve other lanes; reads of undefined temporaries stay unknown.
SM1 texture arithmetic is inventoried but deliberately not interpreted.
"""
from collections import Counter
from dataclasses import dataclass
import re


LANES = 'xyzw'
MODEL = re.compile(r'^\s*(vs|ps)[_.]([123])[_.]([0-4x]|[ab])\s*$', re.I)
REGISTER = re.compile(
    r'^(?P<reg>(?:[rcvtsiabo]\d+|o(?:pos|fog|pts|depth)|o[dtc]\d+|v(?:face|pos)|aL))'
    r'(?P<relative>\[[^\]]+\])?(?P<premodifier>_(?:bias|bx2|x2|dz|dw|abs|not|comp))?'
    r'(?:\.(?P<swizzle>[xyzwrgba]{1,4}))?'
    r'(?P<modifier>_(?:bias|bx2|x2|dz|dw|abs|not|comp))?$', re.I)
FLOW = {'if', 'ifc', 'else', 'endif', 'loop', 'endloop', 'rep', 'endrep',
        'break', 'breakc', 'breakp', 'call', 'callnz', 'ret', 'label'}
ELEMENTWISE = {'mov': 1, 'abs': 1, 'frc': 1, 'mova': 1, 'add': 2, 'sub': 2,
               'mul': 2, 'mad': 3, 'min': 2, 'max': 2, 'slt': 2, 'sge': 2,
               'lrp': 3, 'cmp': 3, 'cnd': 3, 'dsx': 1, 'dsy': 1, 'setp': 2}
SCALAR = {'rcp': 1, 'rsq': 1, 'exp': 1, 'log': 1, 'pow': 2}
DOT = {'dp3': 3, 'dp4': 4}
MATRIX = {'m4x4': (4, 4), 'm4x3': (4, 3), 'm3x4': (3, 4),
          'm3x3': (3, 3), 'm3x2': (3, 2)}
# Broad component unions are safe for these coupled operations, but are not
# claims that every listed component changes every output component.
COUPLED = {'lit': (1, 4), 'dst': (2, 4), 'crs': (2, 3), 'nrm': (1, 3),
           'sgn': (3, 4), 'expp': (1, 4), 'logp': (1, 4)}
# sincos deliberately stays unsupported: its arity and undefined destination
# lanes vary by shader model. A generic component union is not a sound model.
TEXTURES = {'texld': 2, 'texldp': 2, 'texldb': 2, 'texldd': 4, 'texldl': 2}
NO_DEST = FLOW | {'nop', 'phase', 'texkill', 'end'}
KNOWN_MODIFIERS = {'sat', 'pp', 'centroid', 'x2', 'x4', 'x8', 'd2', 'd4', 'd8',
                   'gt', 'ge', 'eq', 'ne', 'lt', 'le'}
HINT_PATTERNS = {
    'position_matrix': r'world|view|proj|matrix|transform',
    'lighting': r'light|ambient|diffuse|specular|emissi|fresnel',
    'fog': r'fog|nebula|haze',
    'time_animation': r'time|anim|phase|scroll|speed|wind|wave|pulse',
    'skinning': r'bone|skin|joint|palette|blendweight|blendindices',
    'depth': r'depth|shadow|zbuffer',
}


@dataclass(frozen=True)
class Dependency:
    sources: frozenset = frozenset()
    unknown: frozenset = frozenset()

    @staticmethod
    def merge(*items):
        return Dependency(frozenset().union(*(d.sources for d in items)),
                          frozenset().union(*(d.unknown for d in items)))


def _register(operand):
    """Parse a register operand without silently accepting trailing syntax."""
    value = operand.strip().lower()
    value = re.sub(r'^[!+\-]', '', value)
    if value.startswith('abs(') and value.endswith(')'):
        value = value[4:-1]
    if value.startswith('1-'):
        value = value[2:]
    match = REGISTER.fullmatch(value)
    if not match:
        return None
    result = match.groupdict()
    result['modifier'] = result['premodifier'] or result['modifier']
    result['swizzle'] = (result['swizzle'] or '').translate(str.maketrans('rgba', LANES))
    return result


def _operands(value):
    # Commas inside bracket/parenthesized source expressions are not separators.
    result, start, depth = [], 0, 0
    for index, char in enumerate(value):
        depth += (char in '[(') - (char in '])')
        if char == ',' and depth == 0:
            result.append(value[start:index].strip())
            start = index + 1
    if value.strip():
        result.append(value[start:].strip())
    return result


def _ctab_parameters(ctab):
    parameters = ctab.get('constants', []) if isinstance(ctab, dict) else (ctab or [])
    result = []
    for parameter in parameters:
        prefix = {0: 'b', 1: 'i', 2: 'c', 3: 's'}.get(parameter.get('register_set'))
        start, count = parameter.get('register'), parameter.get('count')
        if prefix is None or not isinstance(start, int) or not isinstance(count, int):
            continue
        if start < 0 or not 0 < count <= 4096:
            continue
        result.append({'name': str(parameter.get('name', '')), 'register_set': parameter['register_set'],
                       'register': start, 'count': count, 'prefix': prefix})
    return result


def _parameter_refs(registers, parameters):
    result = []
    for parameter in parameters:
        refs = sorted(reg for reg in registers if
                      re.fullmatch(parameter['prefix'] + r'\d+', reg) and
                      parameter['register'] <= int(reg[1:]) < parameter['register'] + parameter['count'])
        if refs:
            result.append({key: value for key, value in parameter.items() if key != 'prefix'} |
                          {'referenced_registers': refs})
    return result


def _implicit_semantic(register, stage):
    if register == 'opos':
        return 'position0'
    if register == 'ofog':
        return 'fog0'
    if register == 'opts':
        return 'psize0'
    if register == 'odepth':
        return 'depth0'
    for prefix, semantic in [('oc', 'color'), ('od', 'color'), ('ot', 'texcoord')]:
        if re.fullmatch(prefix + r'\d+', register):
            return semantic + register[len(prefix):]
    if stage == 'ps' and re.fullmatch(r'v[01]', register):
        return 'color' + register[1:]
    if stage == 'ps' and re.fullmatch(r't\d+', register):
        return 'texcoord' + register[1:]
    return {'vface': 'face0', 'vpos': 'position0'}.get(register)


def analyze(text, ctab=None):
    """Return a derived static inventory; see module docstring for proof limits."""
    lines = text.replace('\x00', '').splitlines()
    markers = [(index, MODEL.fullmatch(line.split('//', 1)[0])) for index, line in enumerate(lines)]
    markers = [(index, match) for index, match in markers if match]
    if not markers:
        return {'schema_version': 1, 'status': 'unknown', 'unknown_reasons': ['missing_shader_model']}
    start, model = markers[-1]
    stage, major, minor = (part.lower() for part in model.groups())
    parameters = _ctab_parameters(ctab)
    instructions, global_unknown = [], set()
    for line in lines[start + 1:]:
        line = line.split('//', 1)[0].strip()
        if not line:
            continue
        if line.startswith('+'):
            global_unknown.add('coissued_instruction')
            line = line[1:].strip()
        if line.startswith('('):
            global_unknown.add('predicated_instruction')
            line = re.sub(r'^\([^)]*\)\s*', '', line)
        match = re.fullmatch(r'([A-Za-z][A-Za-z0-9_]*)(?:\s+(.*))?', line)
        if not match:
            global_unknown.add('unparsed_instruction')
            continue
        spelling = match[1].lower()
        parts = spelling.split('_')
        opcode = parts[0]
        # D3DX spells comparisons if_gt/break_ge, rather than ifc/breakc.
        if opcode == 'if' and any(p in {'gt', 'ge', 'eq', 'ne', 'lt', 'le'} for p in parts[1:]):
            opcode = 'ifc'
        if opcode == 'break' and len(parts) > 1:
            opcode = 'breakc'
        instructions.append((opcode, parts[1:], _operands(match[2] or '')))

    state, declarations, semantics, declared_masks, saturation, relative = {}, [], {}, {}, [], []
    counts, modifiers, reads, writes = Counter(), Counter(), Counter(), Counter()
    texture_sites, depth_sites, kill_sites, matrix_sites, dot_sites = [], [], [], [], []
    unsupported = set()
    output_registers, literal_registers = set(), set()
    defined = {parsed['reg'] for op, _, operands in instructions if op in {'def', 'defi', 'defb'}
               and operands and (parsed := _register(operands[0]))}

    def read(source, lane_indices, instruction_index):
        parsed = _register(source)
        if not parsed:
            return Dependency(unknown=frozenset({'unsupported_source_operand'}))
        register = parsed['reg']
        reads[register] += 1
        swizzle = parsed['swizzle'] or LANES
        if len(swizzle) not in {1, 4}:
            return Dependency(unknown=frozenset({'unsupported_source_swizzle'}))
        swizzle = swizzle if len(swizzle) == 4 else swizzle + swizzle[-1] * (4 - len(swizzle))
        selected = {swizzle[index] for index in lane_indices}
        if parsed['modifier'] in {'_dz', '_dw'}:
            # Legacy projective source modifiers divide by the source Z/W.
            denominator = 2 if parsed['modifier'] == '_dz' else 3
            # Include both pre/post-swizzle lanes conservatively for legacy SM1.
            selected.update({LANES[denominator], swizzle[denominator]})
        if parsed['relative']:
            # The base register is only a syntactic reference, not a bounded
            # constant range. Dynamic indexing can address outside that CTAB item.
            site = {'instruction_index': instruction_index, 'base_register': register}
            if site not in relative:
                relative.append(site)
            address_sources = re.findall(r'(?:a\d+|al)\.[xyzw]', parsed['relative'])
            return Dependency(frozenset([register + '[relative]'] + address_sources),
                              frozenset({'relative_constant_addressing'}))
        dependencies = []
        for lane in selected:
            if register in defined:
                dependencies.append(Dependency(frozenset({'literal:' + register + '.' + lane})))
            elif (register, lane) in state:
                dependencies.append(state[(register, lane)])
            elif re.fullmatch(r'(?:c|b|i|s|v|t)\d+|vface|vpos', register):
                dependencies.append(Dependency(frozenset({register + '.' + lane})))
            else:
                dependencies.append(Dependency(unknown=frozenset({'uninitialized_register:' + register + '.' + lane})))
        return Dependency.merge(*dependencies)

    for index, (opcode, suffixes, operands) in enumerate(instructions):
        counts[opcode] += 1
        modifiers.update(suffixes)
        dest = _register(operands[0]) if operands else None
        if opcode == 'dcl':
            if not dest:
                global_unknown.add('malformed_declaration')
                continue
            semantic_parts = [s for s in suffixes if s not in KNOWN_MODIFIERS]
            semantic = semantic_parts[0] if semantic_parts else _implicit_semantic(dest['reg'], stage)
            if semantic and not semantic[-1].isdigit():
                semantic += '0'
            role = 'output' if dest['reg'].startswith('o') else 'sampler' if dest['reg'].startswith('s') else 'input'
            declarations.append({'register': dest['reg'], 'mask': dest['swizzle'] or LANES,
                                 'semantic': semantic, 'role': role,
                                 'modifiers': [s for s in suffixes if s in KNOWN_MODIFIERS]})
            if semantic:
                semantics[dest['reg']] = semantic
            if role == 'output':
                output_registers.add(dest['reg'])
                declared_masks[dest['reg']] = dest['swizzle'] or LANES
            continue
        if opcode in {'def', 'defi', 'defb'}:
            if dest and len(operands) == (2 if opcode == 'defb' else 5):
                literal_registers.add(dest['reg'])
            else:
                global_unknown.add('malformed_definition')
            continue
        if any(s not in KNOWN_MODIFIERS for s in suffixes):
            global_unknown.add('unsupported_instruction_modifier')
        if opcode in FLOW:
            global_unknown.add('control_flow')
        if opcode == 'texkill':
            kill_sites.append(index)
        if opcode in MATRIX:
            matrix_sites.append({'instruction_index': index, 'opcode': opcode})
        if opcode in DOT or opcode == 'dp2add':
            dot_sites.append({'instruction_index': index, 'opcode': opcode})
        if opcode.startswith('tex') and opcode != 'texkill':
            texture_sites.append({'instruction_index': index, 'opcode': opcode})
        if opcode == 'texdepth':
            depth_sites.append(index)
        if 'sat' in suffixes:
            saturation.append({'instruction_index': index, 'opcode': opcode,
                               'destination_register': dest['reg'] if dest else None,
                               'mask': (dest['swizzle'] or LANES) if dest else None,
                               'source_registers': sorted({p['reg'] for source in operands[1:]
                                                           if (p := _register(source))})})
        # Keep syntactic references even when an instruction cannot be interpreted.
        for source in (operands if opcode in NO_DEST else operands[1:]):
            parsed_source = _register(source)
            if parsed_source:
                reads[parsed_source['reg']] += 1
        if opcode in NO_DEST:
            for operand in operands:
                read(operand, range(4), index)
            continue
        if not dest or dest['relative'] or dest['modifier']:
            global_unknown.add('unsupported_destination_operand')
            continue
        register, mask = dest['reg'], dest['swizzle'] or LANES
        writes[register] += 1
        if register.startswith('o'):
            output_registers.add(register)
        if register == 'odepth':
            depth_sites.append(index)
        sources = operands[1:]
        lane_results = {}
        for lane in mask:
            lane_index = LANES.index(lane)
            expected = ELEMENTWISE.get(opcode, SCALAR.get(opcode))
            if opcode in ELEMENTWISE and len(sources) == expected:
                result = Dependency.merge(*(read(s, [lane_index], index) for s in sources))
            elif opcode in SCALAR and len(sources) == expected:
                result = Dependency.merge(*(read(s, [0], index) for s in sources))
            elif opcode in DOT and len(sources) == 2:
                result = Dependency.merge(*(read(s, range(DOT[opcode]), index) for s in sources))
            elif opcode == 'dp2add' and len(sources) == 3:
                result = Dependency.merge(read(sources[0], range(2), index),
                                          read(sources[1], range(2), index), read(sources[2], [0], index))
            elif opcode in MATRIX and len(sources) == 2:
                width, height = MATRIX[opcode]
                matrix = _register(sources[1])
                if (matrix and re.fullmatch(r'c\d+', matrix['reg']) and not matrix['relative']
                        and not matrix['modifier'] and not matrix['swizzle'] and lane_index < height):
                    row = 'c' + str(int(matrix['reg'][1:]) + lane_index)
                    result = Dependency.merge(read(sources[0], range(width), index), read(row, range(width), index))
                else:
                    result = Dependency(unknown=frozenset({'unsupported_matrix_addressing_or_lane'}))
            elif opcode in COUPLED and len(sources) == COUPLED[opcode][0]:
                result = Dependency.merge(*(read(s, range(COUPLED[opcode][1]), index) for s in sources))
                if opcode in {'nrm', 'crs'} and lane == 'w':
                    result = Dependency.merge(result, Dependency(unknown=frozenset({'undefined_vector_result_w'})))
            elif opcode in TEXTURES and len(sources) == TEXTURES[opcode]:
                result = Dependency.merge(*(read(s, range(4), index) for s in sources))
                samplers = [_register(s) for s in sources]
                texture_sources = {'texture:' + p['reg'] for p in samplers if p and p['reg'].startswith('s')}
                if not texture_sources:
                    result = Dependency.merge(result, Dependency(unknown=frozenset({'missing_sampler_operand'})))
                result = Dependency.merge(result, Dependency(frozenset(texture_sources)))
            else:
                unsupported.add(opcode)
                global_unknown.add('unsupported_instruction')
                result = Dependency.merge(*(read(s, range(4), index) for s in sources),
                                          Dependency(unknown=frozenset({'unsupported_opcode_or_arity:' + opcode})))
            lane_results[lane] = result
        # Commit after evaluating all lanes: a swizzled self-write reads the old
        # register for the whole instruction, never partially updated lanes.
        for lane, result in lane_results.items():
            state[(register, lane)] = result

    if stage == 'ps' and major == '1':
        output_registers.add('r0')
        semantics['r0'] = 'color0'

    def summarize(dependency):
        unknown = sorted(dependency.unknown | global_unknown)
        sources = sorted(dependency.sources)
        registers = sorted({s.split('.', 1)[0] for s in sources if ':' not in s})
        return {'status': 'unknown' if unknown else 'complete_conservative',
                'unknown_reasons': unknown, 'source_components': sources,
                'source_registers': registers,
                'input_semantics': sorted({semantics.get(r) or _implicit_semantic(r, stage)
                                           for r in registers if (semantics.get(r) or _implicit_semantic(r, stage))
                                           and (r.startswith('v') or r.startswith('t'))}),
                'parameters': _parameter_refs(registers, parameters)}

    outputs = []
    for register in sorted(output_registers):
        semantic = semantics.get(register) or _implicit_semantic(register, stage)
        required_lanes = 'x' if register in {'odepth', 'ofog', 'opts'} else declared_masks.get(register, LANES)
        if semantic and (semantic.startswith('color') or semantic == 'position0'):
            required_lanes = LANES
        lane_deps = {lane: state.get((register, lane), Dependency(unknown=frozenset({'unwritten_output_lane'})))
                     for lane in required_lanes}
        output = {'register': register, 'semantic': semantic,
                  'lanes': {lane: summarize(dep) for lane, dep in lane_deps.items()},
                  'all': summarize(Dependency.merge(*lane_deps.values()))}
        if semantic and semantic.startswith('color'):
            output['rgb'] = summarize(Dependency.merge(*(lane_deps[lane] for lane in 'xyz')))
            output['alpha'] = summarize(lane_deps['w'])
        outputs.append(output)
    all_refs = set(reads)
    hints = {kind: [p['name'] for p in parameters if re.search(pattern, p['name'], re.I)]
             for kind, pattern in HINT_PATTERNS.items()}
    return {'schema_version': 1, 'status': 'unknown' if global_unknown else 'parsed',
            'stage': stage, 'model': major + '_' + minor,
            'preshader_excluded': any('preshader' in line.lower() for line in lines[:start]),
            'shader_model_sections': len(markers), 'instruction_count': len(instructions),
            'instruction_index_kind': 'zero_based_disassembly_ordinal_including_dcl_def',
            'opcode_counts': dict(sorted(counts.items())), 'modifier_counts': dict(sorted(modifiers.items())),
            'declarations': declarations, 'read_registers': sorted(reads), 'written_registers': sorted(writes),
            'literal_registers': sorted(literal_registers), 'parameter_references': _parameter_refs(all_refs, parameters),
            'name_hints_only': hints, 'control_flow_counts': {op: counts[op] for op in sorted(FLOW) if counts[op]},
            'texture_fetches': texture_sites, 'vertex_texture_fetch': stage == 'vs' and bool(texture_sites),
            'depth_write_instruction_indices': sorted(set(depth_sites)), 'texkill_instruction_indices': kill_sites,
            'saturation_sites': saturation, 'relative_addressing_sites': relative,
            'matrix_operations': matrix_sites, 'dot_operations': dot_sites, 'outputs': outputs,
            'position_outputs': [o for o in outputs if o['semantic'] == 'position0'],
            'unsupported_opcodes': sorted(unsupported), 'unknown_reasons': sorted(global_unknown),
            'limitations': ['Static dependencies are conservative, not algebraic equivalence or runtime facts.',
                            'Control flow/coissue/predication/unsupported instructions invalidate all output proofs.',
                            'Relative-address base references do not bound the runtime constant range.',
                            'CTAB name hints do not establish shader or engine semantics.',
                            'Hardware constant dependencies do not resolve preshader/host provenance.',
                            'Instruction ordinals are not bytecode token offsets.']}
