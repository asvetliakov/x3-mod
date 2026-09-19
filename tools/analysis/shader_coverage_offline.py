#!/usr/bin/env python3
"""Offline coverage census of every shader pair the installed archives can bind.

Proactive counterpart to the log-derived census (`docs/verification/
shader-coverage-census.md`): instead of waiting for a flight to hit an unknown
vertex/pixel pair, this enumerates the pairs from the game's own data and says,
per pair, whether the motion/sun-shadow registry knows it and whether the
offline rewriter model would accept it.

What the game ships is already compiled: every `shader/**/*.fb` archive entry
is a D3DXFX container (magic `0xfeff0901`) holding the shader blobs, so no
D3DX compile step and no Wine run is needed. The variants the game can select
are directories inside the archive -- the shader-profile trees `1_1`, `1_4`,
`2_0`, `2_a`, `2_b`, `3_0` and the toggle trees `hueshift_off`,
`hue_lights_off`, `v_lights_off` -- and all of them are enumerated.

Steps:
  1. The effective effect set, with the game's override precedence: a virtual
     path present in several catalogues resolves to the last one (ascending
     numbered catalogues, `addon/` last), via `effect_passes._archive_effects`.
  2. Per technique pass, the vertex and pixel program identity exactly as the
     proxy computes it at CreateShader time: FNV-1a 64 over the complete token
     stream (version, comments/CTAB and END included), plus DWORD length and
     version token -- `index_shaders.fnv1a64` / `shader_end`, the same
     functions that produced the registry rows.
  3. A render-state class per pass from the pass's own state assignments:
     opaque depth writer, colour-masked depth-only, blended, additive, or
     "dynamic" where the effect drives the state from a parameter or a
     preshader expression and the value is only known at draw time.
  4. The registry verdict: a pair row in `motion_output_profiles_inc.h`
     (what `material_motion_profile` looks up, and whose absence produces the
     `unmatched=unregistered` refusal in `MotionOutput::evaluate_draw`), or a
     jitter-only vertex row in `depth_prepass_profiles.h`.
  5. For unregistered pairs, the rewriter verdict from the generator model
     (`inspect_motion_output_profiles`): transformation class A-D means the
     row could be added mechanically; anything else carries its blocking
     reasons (register collisions with the reserved c216-c220 range, slot
     budget, position pattern, shader model).
  6. A cross-check against flight evidence: every (vs, ps) pair observed in a
     session log must appear in the offline set, which is what validates the
     hash method and the enumeration.

Only derived facts leave this module: catalogue and virtual paths, technique
and pass names, hashes, lengths, state values and counts. No shader words, no
extracted archive bytes.
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
from effect_passes import (_archive_effects, _u32, parse_container,  # noqa: E402
                           STATE_TABLE, VERTEX_SHADER_STATE, PIXEL_SHADER_STATE)
from index_shaders import fnv1a64, shader_end  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402

NULL_HASH = '0' * 16

# Render-state operations the pass class depends on (`effect_passes.STATE_TABLE`).
Z_ENABLE, Z_WRITE_ENABLE, ALPHA_TEST_ENABLE = 0, 3, 4
SRC_BLEND, DEST_BLEND, ALPHA_BLEND_ENABLE = 6, 7, 13
COLOR_WRITE_ENABLE = 73
CLASS_STATES = {Z_ENABLE: 'zenable', Z_WRITE_ENABLE: 'zwrite',
                ALPHA_TEST_ENABLE: 'atest', SRC_BLEND: 'src', DEST_BLEND: 'dst',
                ALPHA_BLEND_ENABLE: 'blend', COLOR_WRITE_ENABLE: 'mask'}
for _operation, _expected in ((Z_ENABLE, 'ZEnable'), (Z_WRITE_ENABLE, 'ZWriteEnable'),
                              (ALPHA_TEST_ENABLE, 'AlphaTestEnable'), (SRC_BLEND, 'SrcBlend'),
                              (DEST_BLEND, 'DestBlend'), (ALPHA_BLEND_ENABLE, 'AlphaBlendEnable'),
                              (COLOR_WRITE_ENABLE, 'ColorWriteEnable')):
    assert STATE_TABLE[_operation][0] == _expected, (_operation, STATE_TABLE[_operation])
D3DBLEND_ONE = 2
# Transformation classes the generator emits as table rows.
REWRITABLE_CLASSES = ('A_reference_registers', 'B_relocated_registers',
                      'C_relocated_registers_with_static_branches',
                      'D_bounded_damage_branches')
# Pass classes that can punch depth for a later jittered draw; these are the
# ones an unregistered pair must not be found in.
DEPTH_CLASSES = ('opaque_depth', 'depth_only_mask', 'depth_dynamic')


def _state_value(data, container, technique_index, pass_index, state_index, state):
    """`(source, value)` of one pass state: a literal DWORD or a dynamic source.

    A state record whose value is produced at draw time carries a resource:
    usage 0 is a compiled FXLC preshader, 1 a parameter reference, 2 an
    indexed parameter reference. Only a record without a resource is constant
    in the container.
    """
    operation, _, _, value_offset = state
    for key in ((technique_index, pass_index, 0xffffffff, state_index),
                (technique_index, pass_index, 0, state_index)):
        resource = container['resources'].get(key)
        if resource is not None:
            return {0: 'expression', 1: 'parameter', 2: 'array_selector'}.get(
                resource[0], 'usage_%d' % resource[0]), None
    del operation
    return 'literal', _u32(data, container['base'] + value_offset)[0]


def _program(data, container, technique_index, pass_index, state_index):
    """`(hash, dwords, version)` of a shader state, or `None` for a NULL shader."""
    resource = container['state_resources'].get((technique_index, pass_index, state_index))
    if resource is None:
        return None
    usage, start, size = resource
    if usage != 0 or size < 8:
        return None
    words = struct.unpack('<%dI' % (size // 4), data[start:start + size // 4 * 4])
    end = shader_end(words, 0)
    if end is None:
        return None
    return fnv1a64(data[start:start + 4 * end]), end, words[0]


def classify_pass(states):
    """The render-state class of one pass from its own state assignments.

    `states` maps the short name of `CLASS_STATES` to a literal value, to
    `'dynamic'` when the effect computes it at draw time, or is absent when
    the pass leaves the device state alone. An absent depth state is reported
    as `depth_inherited`: the pass writes depth if the previous draw did, so
    it is treated as a depth candidate, never as proven harmless.
    """
    zwrite, blend = states.get('zwrite'), states.get('blend')
    mask, dst = states.get('mask'), states.get('dst')
    if zwrite == 'dynamic':
        return 'depth_dynamic'
    if zwrite == 1:
        if mask == 0:
            return 'depth_only_mask'
        if blend == 1:
            return 'blended_depth'
        return 'opaque_depth'
    if zwrite == 0:
        if blend == 1 or blend == 'dynamic':
            return 'additive' if dst == D3DBLEND_ONE else 'blended'
        return 'no_depth'
    return 'depth_inherited'


def archive_pairs(game):
    """Every (vs, ps) pair bound by a pass of the effective installed effects."""
    pairs, effects, passes = {}, 0, 0
    catalogues, incomplete = Counter(), 0
    # How many entries the override precedence hides, from the catalogue
    # listings alone (no DAT read): one virtual path, several catalogues.
    seen_paths = Counter(
        entry['path'] for cat in sorted([*game.glob('[0-9][0-9].cat'),
                                         *game.glob('addon/[0-9][0-9].cat')])
        for entry in read_catalogue(cat)
        if entry['path'].startswith('shader/') and entry['path'].endswith('.fb'))
    overridden = sum(count - 1 for count in seen_paths.values())
    for catalogue, path, data in _archive_effects(game):
        effects += 1
        catalogues[catalogue] += 1
        container = parse_container(data)
        directory = str(Path(path).parent)
        parts = directory.split('/')
        profile = parts[1] if len(parts) > 1 else '(none)'
        toggle = parts[2] if len(parts) > 2 else '(base)'
        for technique_index, (technique, technique_passes) in enumerate(container['techniques']):
            for pass_index, (pass_name, pass_states) in enumerate(technique_passes):
                passes += 1
                programs, values = {}, {}
                for state_index, state in enumerate(pass_states):
                    operation = state[0]
                    if operation in (VERTEX_SHADER_STATE, PIXEL_SHADER_STATE):
                        stage = 'vs' if operation == VERTEX_SHADER_STATE else 'ps'
                        programs[stage] = _program(data, container, technique_index,
                                                   pass_index, state_index)
                        continue
                    name = CLASS_STATES.get(operation)
                    if name is None:
                        continue
                    source, value = _state_value(data, container, technique_index,
                                                 pass_index, state_index, state)
                    values[name] = value if source == 'literal' else 'dynamic'
                vertex, pixel = programs.get('vs'), programs.get('ps')
                if vertex is None and pixel is None:
                    incomplete += 1
                    continue
                key = (vertex[0] if vertex else NULL_HASH, pixel[0] if pixel else NULL_HASH)
                record = pairs.setdefault(key, {
                    'vs': key[0], 'ps': key[1],
                    'vs_dwords': vertex[1] if vertex else 0,
                    'ps_dwords': pixel[1] if pixel else 0,
                    'vs_model': _model(vertex), 'ps_model': _model(pixel),
                    'pass_occurrences': 0, 'classes': Counter(), 'basenames': set(),
                    'techniques': set(), 'pass_names': set(), 'profiles': set(),
                    'toggles': set(), 'catalogues': set(), 'paths': set()})
                record['pass_occurrences'] += 1
                record['classes'][classify_pass(values)] += 1
                record['basenames'].add(Path(path).stem)
                record['techniques'].add(technique)
                record['pass_names'].add(pass_name)
                record['profiles'].add(profile)
                record['toggles'].add(toggle)
                record['catalogues'].add(catalogue)
                record['paths'].add(path)
    return {'effect_count': effects, 'overridden_entry_count': overridden,
            'pass_count': passes, 'incomplete_pass_count': incomplete,
            'catalogues': dict(sorted(catalogues.items())), 'pairs': pairs}


def _model(program):
    if program is None:
        return None
    version = program[2]
    return '%d_%d' % ((version >> 8) & 255, version & 255)


def registered_pairs(header):
    """The (vs, ps) rows of the generated profile table, as hex-string pairs."""
    text = header.read_text()
    rows = []
    for match in re.finditer(r'^\{0x([0-9a-f]{16})ull,[^\n]*\n\s*0x([0-9a-f]{16})ull',
                             text, re.MULTILINE):
        rows.append((match.group(1), match.group(2)))
    if not rows:
        raise ValueError('no profile rows parsed from %s' % header)
    return rows


def prepass_vertices(header):
    """The jitter-only depth-prepass vertex fingerprints."""
    body = header.read_text()
    start = body.index('depth_prepass_profiles[]')
    end = body.index('};', start)
    return [match.group(1) for match in
            re.finditer(r'\{0x([0-9a-f]{16})ull', body[start:end])]


def rewriter_verdicts(profiles):
    """Pair -> the generator's verdict, from a motion-output profile result."""
    verdicts = {}
    for pair in profiles.get('pairs', ()):
        rewritable = pair['transformation_class'] in REWRITABLE_CLASSES
        verdicts[(pair['vs'], pair['ps'])] = {
            'model': 'sm3', 'rewritable': rewritable,
            'transformation_class': pair['transformation_class'],
            'reasons': pair['blocking_reasons']}
    for pair in profiles.get('sm2_pairs', ()):
        verdicts[(pair['vs'], pair['ps'])] = {
            'model': 'sm2', 'rewritable': False, 'transformation_class': pair['group'],
            'reasons': pair['reasons'] or ['sm2_pair_has_no_generated_row']}
    for pair in profiles.get('sm1_pairs', ()):
        verdicts[(pair['vs'], pair['ps'])] = {
            'model': 'sm1', 'rewritable': False, 'transformation_class': 'sm1',
            'reasons': [pair['reason']]}
    return verdicts


def flight_pairs(path):
    """`(vs, ps)` pairs observed in flight, from `vs=<hex> ps=<hex>` text."""
    pattern = re.compile(r'vs=([0-9a-f]{16})\s+ps=([0-9a-f]{16})')
    return sorted({match.groups() for match in pattern.finditer(path.read_text())})


def census(archive, rows, prepass_rows, verdicts, observed):
    """The census from an `archive_pairs` result and the registry/rewriter facts."""
    registered = set(rows)
    prepass = set(prepass_rows)
    pairs, counts, unknown_depth = [], Counter(), []
    for key in sorted(archive['pairs']):
        record = archive['pairs'][key]
        classes = record['classes']
        depth = any(classes[name] for name in DEPTH_CLASSES) or bool(classes['depth_inherited'])
        if key in registered:
            coverage = 'registered'
        elif record['vs'] in prepass:
            coverage = 'prepass_jitter_only'
        else:
            verdict = verdicts.get(key)
            if record['ps'] == NULL_HASH:
                coverage = 'not_rewritable'
            elif verdict is None:
                coverage = 'not_analyzed'
            else:
                coverage = 'rewritable_unregistered' if verdict['rewritable'] else 'not_rewritable'
        reasons = []
        if coverage in ('not_rewritable', 'not_analyzed'):
            verdict = verdicts.get(key)
            if record['ps'] == NULL_HASH:
                reasons = ['null_pixel_shader: the pass binds no PS, so there is no '
                           'program to append the motion write to']
            elif verdict is None:
                reasons = ['pair absent from the motion-output profile result']
            else:
                reasons = verdict['reasons']
        entry = {'vs': record['vs'], 'ps': record['ps'],
                 'vs_model': record['vs_model'], 'ps_model': record['ps_model'],
                 'vs_dwords': record['vs_dwords'], 'ps_dwords': record['ps_dwords'],
                 'coverage': coverage, 'depth_candidate': depth,
                 'classes': dict(sorted(classes.items())),
                 'pass_occurrences': record['pass_occurrences'],
                 'transformation_class': (verdicts.get(key) or {}).get('transformation_class'),
                 'reasons': reasons,
                 'basenames': sorted(record['basenames']),
                 'techniques': sorted(record['techniques']),
                 'pass_names': sorted(record['pass_names']),
                 'profile_directories': sorted(record['profiles']),
                 'toggle_directories': sorted(record['toggles']),
                 'catalogues': sorted(record['catalogues']),
                 'observed_in_flight': list(key) in [list(p) for p in observed] if observed else False}
        counts[coverage] += 1
        if depth:
            counts['depth_candidate_' + coverage] += 1
            if coverage not in ('registered', 'prepass_jitter_only'):
                unknown_depth.append(entry)
        pairs.append(entry)
    offline = {(entry['vs'], entry['ps']) for entry in pairs}
    missing = [list(pair) for pair in observed if tuple(pair) not in offline]
    unknown_depth.sort(key=lambda entry: (-entry['pass_occurrences'], entry['vs'], entry['ps']))
    return {
        'schema': 1,
        'scope': 'Offline (vs, ps) coverage census of the installed compiled effects: '
                 'derived names, hashes, lengths, state values and counts only.',
        'tool_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'archive': {key: archive[key] for key in
                    ('effect_count', 'overridden_entry_count', 'pass_count',
                     'incomplete_pass_count', 'catalogues')},
        'registry': {'profile_rows': len(rows), 'distinct_registered_pairs': len(registered),
                     'prepass_vertex_rows': len(prepass),
                     'registered_rows_absent_from_archive':
                         sorted(list(pair) for pair in registered - set(archive['pairs']))},
        'pair_count': len(pairs),
        'coverage_counts': dict(sorted(counts.items())),
        'coverage_by_model': dict(sorted(Counter(
            '%s/%s:%s' % (entry['vs_model'], entry['ps_model'] or 'null', entry['coverage'])
            for entry in pairs).items())),
        # Split the unknown depth candidates by the strength of the evidence: a
        # pass that sets ZWriteEnable to 1 in the container is a proven depth
        # writer; the rest only become one when the effect's preshader or a
        # parameter turns depth writing on at draw time.
        'unknown_depth_breakdown': dict(sorted(Counter(
            'literal_depth_writer' if any(entry['classes'].get(name)
                                          for name in ('opaque_depth', 'depth_only_mask',
                                                       'blended_depth'))
            else 'dynamic_or_inherited'
            for entry in unknown_depth).items())),
        'class_counts': dict(sorted(Counter(
            name for entry in pairs for name in entry['classes']).items())),
        'flight_cross_check': {'observed_pairs': len(observed),
                               'observed': [list(pair) for pair in observed],
                               'missing_from_offline_set': missing,
                               'passed': not missing},
        'unknown_depth_pairs': unknown_depth,
        'pairs': pairs,
        'limitations': [
            'Pass pairings come from the installed archives; an effect the engine '
            'builds or overrides at run time is not enumerated.',
            'A state the effect drives from a parameter or preshader is reported '
            'as dynamic, and the pass counts as a depth candidate.',
            'A pass that assigns no depth state inherits the device state; it is '
            'classed depth_inherited, not proven harmless.',
            'Rewritability is the generator model\'s verdict; the C++ transformer '
            'revalidates every structural assumption before it splices.']}


def markdown_table(result, limit=40):
    """The unknown depth-writing pairs, by effect file and technique."""
    lines = ['| vs | ps | models | class | effects | techniques | passes | verdict |',
             '| --- | --- | --- | --- | --- | --- | ---: | --- |']
    for entry in result['unknown_depth_pairs'][:limit]:
        classes = ', '.join('%s=%d' % item for item in entry['classes'].items())
        lines.append('| `%s` | `%s` | %s/%s | %s | %s | %s | %d | %s |' % (
            entry['vs'], entry['ps'], entry['vs_model'], entry['ps_model'] or '-',
            classes, ', '.join(entry['basenames'][:4]), ', '.join(entry['techniques'][:4]),
            entry['pass_occurrences'], entry['coverage']))
    return '\n'.join(lines)


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=Path, required=True,
                        help='installed game directory holding the numbered CAT/DAT archives')
    parser.add_argument('--profiles', type=Path,
                        default=root / 'verification/results/motion-output-profiles.json',
                        help='motion-output profile result used as the rewriter verdict')
    parser.add_argument('--inventory', type=Path,
                        help='recompute the rewriter verdicts from a shader-sweep inventory')
    parser.add_argument('--raw-directory', type=Path,
                        help='local program directory for --inventory (bytes are read, never copied)')
    parser.add_argument('--header', type=Path,
                        default=root / 'src/renderer/motion_output_profiles_inc.h')
    parser.add_argument('--prepass-header', type=Path,
                        default=root / 'src/renderer/depth_prepass_profiles.h')
    parser.add_argument('--flight-pairs', type=Path,
                        help='text holding `vs=<hex> ps=<hex>` observations to cross-check')
    parser.add_argument('--output', type=Path, help='machine-readable census JSON')
    parser.add_argument('--markdown', type=Path, help='unknown depth-pair table (Markdown)')
    arguments = parser.parse_args()
    if arguments.inventory:
        if not arguments.raw_directory:
            parser.error('--inventory needs --raw-directory')
        from effect_passes import archive_passes
        from inspect_motion_output_profiles import build
        inventory_bytes = arguments.inventory.read_bytes()
        inventory = json.loads(inventory_bytes)
        inventory['_sha256'] = hashlib.sha256(inventory_bytes).hexdigest()
        profiles = build(inventory, arguments.raw_directory,
                         archive_passes(arguments.game), None)
        profiles_provenance = {'source': 'recomputed', 'inventory': str(arguments.inventory)}
    else:
        profile_bytes = arguments.profiles.read_bytes()
        profiles = json.loads(profile_bytes)
        profiles_provenance = {'source': str(arguments.profiles),
                               'sha256': hashlib.sha256(profile_bytes).hexdigest()}
    observed = flight_pairs(arguments.flight_pairs) if arguments.flight_pairs else []
    result = census(archive_pairs(arguments.game), registered_pairs(arguments.header),
                    prepass_vertices(arguments.prepass_header),
                    rewriter_verdicts(profiles), observed)
    result['rewriter_verdict_source'] = profiles_provenance
    if arguments.output:
        # Archive scale: compact, stable JSON; query it with a script, never
        # read it whole (AGENTS.md, context discipline).
        arguments.output.write_text(json.dumps(result, separators=(',', ':')) + '\n')
    if arguments.markdown:
        arguments.markdown.write_text(markdown_table(result) + '\n')
    print(json.dumps({'archive': result['archive'], 'registry': result['registry'],
                      'pair_count': result['pair_count'],
                      'coverage_counts': result['coverage_counts'],
                      'class_counts': result['class_counts'],
                      'coverage_by_model': result['coverage_by_model'],
                      'unknown_depth_pairs': len(result['unknown_depth_pairs']),
                      'unknown_depth_breakdown': result['unknown_depth_breakdown'],
                      'flight_cross_check': result['flight_cross_check']}, indent=2))
    return 0 if result['flight_cross_check']['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
