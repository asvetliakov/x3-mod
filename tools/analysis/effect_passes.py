#!/usr/bin/env python3
"""Enumerate the technique/pass shader pairings of the installed compiled effects.

The shader sweep records which programs each effect contains, not which vertex
and pixel program a pass binds together. This reads every `shader/**/*.fb`
entry of the numbered CAT/DAT archives in memory (the same decode as
`index_shaders.py`), walks the compiled D3DX effect container and records, per
technique pass, the FNV-1a 64 identity of the vertex and pixel program states.
Only derived facts leave this module: catalogue, virtual path, technique and
pass names, state operations, program hashes and lengths. No effect bytes,
shader words or parameter values are written anywhere.

Container layout (D3DXFX version `0xfeff0901`; the same layout the Wine
d3dx9 implementation parses):

    u32 magic, u32 offset                 -- header; every offset below is
                                             relative to the byte after it
    at 8 + offset:
      u32 parameter_count, technique_count, unknown, object_count
      parameters:  u32 typedef, value, flags, annotations; 2 u32 per annotation
      techniques:  u32 name, annotations, passes; 2 u32 per annotation
        passes:    u32 name, annotations, states; 2 u32 per annotation
          states:  u32 operation, index, typedef, value
      u32 object_data_count, resource_count
      object data: u32 id, size, bytes padded to 4
      resources:   u32 technique, pass, element, state, usage; u32 size, bytes
                   padded to 4 (usage 0 on a shader state: the compiled program)

State operations 146 and 147 are the VertexShader and PixelShader entries of
the D3DX state table. A shader state with no resource record is a NULL shader
(for example the z-only effects bind no pixel shader).

`--classification` additionally classifies every state assignment a pass
applies -- constant render states, constant sampler states, texture bindings,
shader bindings and the parameter-driven forms -- and writes the counts (see
`archive_classification`); `--pass <effect>:<technique>:<pass>` prints one
pass. A resource record whose technique is `0xffffffff` belongs to parameter
`pass` instead: the value of a sampler parameter is `u32 state_count` followed
by the same four-dword state records, repeated per array element, and its
second dword is not a stage index (observed constant `0x100`). Sampler blocks
reach a pass through the sampler names in the pass programs' CTAB, which is
what D3DX resolves inside `BeginPass`.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from index_shaders import fnv1a64, shader_end  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from shader_constants import parse_ctab  # noqa: E402

MAGIC = b'\x01\x09\xff\xfe'
VERTEX_SHADER_STATE = 146
PIXEL_SHADER_STATE = 147
STATE_STAGES = {VERTEX_SHADER_STATE: 'vs', PIXEL_SHADER_STATE: 'ps'}


class MalformedEffect(ValueError):
    """The container does not follow the documented layout."""


def _u32(data, at, count=1):
    if at + 4 * count > len(data):
        raise MalformedEffect('record exceeds effect at %d' % at)
    return struct.unpack_from('<%dI' % count, data, at)


def _name(data, base, offset):
    (size,) = _u32(data, base + offset)
    start = base + offset + 4
    if start + size > len(data):
        raise MalformedEffect('name exceeds effect')
    return data[start:start + size].split(b'\0', 1)[0].decode('latin-1')


def _typedef(data, base, offset):
    """The leading fields every parameter typedef record shares."""
    type_id, class_id, name_offset, _, element_count = _u32(data, base + offset, 5)
    return {'type': type_id, 'class': class_id, 'element_count': element_count,
            'name': _name(data, base, name_offset) if name_offset else ''}


def parse_container(data):
    """Walk one compiled effect and return its records without their values.

    `parameters` are the top-level parameter descriptors (index, name, type,
    class, element count and the two offsets); `techniques` carry per pass the
    raw state records `(operation, index, typedef_offset, value_offset)`;
    `objects` maps object id to `(start, size)`; `resources` maps the full
    `(technique, index, element, state)` key of a resource record to
    `(usage, start, size)`, where `technique == 0xffffffff` means the record
    belongs to parameter `index` (a sampler state block) rather than to a pass.
    `state_resources` is the same table keyed by `(technique, index, state)`
    for the technique-scope lookups that ignore the element.
    """
    if data[:4] != MAGIC:
        raise MalformedEffect('unexpected compiled effect magic')
    _, offset = _u32(data, 0, 2)
    base = 8
    at = base + offset
    parameter_count, technique_count, _, _ = _u32(data, at, 4)
    at += 16
    parameters = []
    for index in range(parameter_count):
        typedef_offset, value_offset, flags, annotations = _u32(data, at, 4)
        at += 16 + 8 * annotations
        record = {'index': index, 'typedef_offset': typedef_offset,
                  'value_offset': value_offset, 'flags': flags,
                  'annotations': annotations}
        record.update(_typedef(data, base, typedef_offset))
        parameters.append(record)
    techniques = []
    for _ in range(technique_count):
        name, annotations, pass_count = _u32(data, at, 3)
        at += 12 + 8 * annotations
        passes = []
        for _ in range(pass_count):
            pass_name, pass_annotations, state_count = _u32(data, at, 3)
            at += 12 + 8 * pass_annotations
            states = []
            for _ in range(state_count):
                states.append(_u32(data, at, 4))
                at += 16
            passes.append((_name(data, base, pass_name), states))
        techniques.append((_name(data, base, name), passes))
    object_count, resource_count = _u32(data, at, 2)
    at += 8
    objects = {}
    for _ in range(object_count):
        identifier, size = _u32(data, at, 2)
        at += 8
        if at + size > len(data):
            raise MalformedEffect('object data exceeds effect')
        objects[identifier] = (at, size)
        at += (size + 3) & ~3
    resources, state_resources = {}, {}
    for _ in range(resource_count):
        technique, index, element, state, usage = _u32(data, at, 5)
        (size,) = _u32(data, at + 20)
        at += 24
        if at + size > len(data):
            raise MalformedEffect('resource exceeds effect')
        resources[(technique, index, element, state)] = (usage, at, size)
        if technique != 0xffffffff:
            state_resources[(technique, index, state)] = (usage, at, size)
        at += (size + 3) & ~3
    if at != len(data):
        raise MalformedEffect('trailing bytes after the resource section')
    return {'base': base, 'parameters': parameters, 'techniques': techniques,
            'objects': objects, 'resources': resources,
            'state_resources': state_resources}


def parse_effect(data):
    """Return the passes of one compiled effect with their shader states.

    Each pass is a dict with technique/pass names and `states`: a list of
    (operation, stage or None, fnv1a64 or None, dword_count or None, status)
    for the shader states only. `status` is `program` for a complete token
    stream, `null` when the state has no resource, `not_a_program` when the
    resource bytes are not one complete SM1-3 stream.
    """
    container = parse_container(data)
    techniques, resources = container['techniques'], container['state_resources']
    result = []
    for technique_index, (technique, passes) in enumerate(techniques):
        for pass_index, (pass_name, states) in enumerate(passes):
            shader_states = []
            for state_index, (operation, _, _, _) in enumerate(states):
                stage = STATE_STAGES.get(operation)
                if stage is None:
                    continue
                resource = resources.get((technique_index, pass_index, state_index))
                if resource is None:
                    shader_states.append((operation, stage, None, None, 'null'))
                    continue
                usage, start, size = resource
                if usage != 0:
                    shader_states.append((operation, stage, None, None, 'usage_%d' % usage))
                    continue
                words = struct.unpack('<%dI' % (size // 4), data[start:start + size // 4 * 4])
                end = shader_end(words, 0) if words else None
                if end is None:
                    shader_states.append((operation, stage, None, None, 'not_a_program'))
                    continue
                code = data[start:start + 4 * end]
                shader_states.append((operation, stage, fnv1a64(code), end, 'program'))
            result.append({'technique_index': technique_index, 'technique': technique,
                           'pass_index': pass_index, 'pass': pass_name, 'states': shader_states})
    return result


def _build_state_table():
    """The D3DX effect state table: operation number -> (name, category).

    The order is D3DX's own; three anchors fix it against the archive data:
    `Lighting` 48 and `SeparateAlphaBlendEnable` 99 (the parameters driving
    those states in `shader/1_1/adeffects.fb` are named `g_EnableFog`/
    `g_SeparateAlphaBlend` and carry the matching BOOL/INT types), the
    verified `VertexShader` 146 / `PixelShader` 147, and the sampler block of
    the same effect, whose eight states decode as Texture 164, MinFilter 170,
    MagFilter 169, MipFilter 171, AddressU 165, AddressV 166, MaxAnisotropy
    174, MaxMipLevel 173 -- the source order of an ordinary `sampler_state`.
    """
    render = ['ZEnable', 'FillMode', 'ShadeMode', 'ZWriteEnable', 'AlphaTestEnable',
              'LastPixel', 'SrcBlend', 'DestBlend', 'CullMode', 'ZFunc', 'AlphaRef',
              'AlphaFunc', 'DitherEnable', 'AlphaBlendEnable', 'FogEnable',
              'SpecularEnable', 'FogColor', 'FogTableMode', 'FogStart', 'FogEnd',
              'FogDensity', 'RangeFogEnable', 'StencilEnable', 'StencilFail',
              'StencilZFail', 'StencilPass', 'StencilFunc', 'StencilRef', 'StencilMask',
              'StencilWriteMask', 'TextureFactor']
    render += ['Wrap%d' % i for i in range(16)]
    render += ['Clipping', 'Lighting', 'Ambient', 'FogVertexMode', 'ColorVertex',
               'LocalViewer', 'NormalizeNormals', 'DiffuseMaterialSource',
               'SpecularMaterialSource', 'AmbientMaterialSource', 'EmissiveMaterialSource',
               'VertexBlend', 'ClipPlaneEnable', 'PointSize', 'PointSize_Min',
               'PointSpriteEnable', 'PointScaleEnable', 'PointScale_A', 'PointScale_B',
               'PointScale_C', 'MultiSampleAntialias', 'MultiSampleMask', 'PatchEdgeStyle',
               'DebugMonitorToken', 'PointSize_Max', 'IndexedVertexBlendEnable',
               'ColorWriteEnable', 'TweenFactor', 'BlendOp', 'PositionDegree',
               'NormalDegree', 'ScissorTestEnable', 'SlopeScaleDepthBias',
               'AntiAliasedLineEnable', 'MinTessellationLevel', 'MaxTessellationLevel',
               'AdaptiveTess_X', 'AdaptiveTess_Y', 'AdaptiveTess_Z', 'AdaptiveTess_W',
               'EnableAdaptiveTessellation', 'TwoSidedStencilMode', 'CCW_StencilFail',
               'CCW_StencilZFail', 'CCW_StencilPass', 'CCW_StencilFunc',
               'ColorWriteEnable1', 'ColorWriteEnable2', 'ColorWriteEnable3',
               'BlendFactor', 'SRGBWriteEnable', 'DepthBias', 'SeparateAlphaBlendEnable',
               'SrcBlendAlpha', 'DestBlendAlpha', 'BlendOpAlpha']
    stage = ['ColorOp', 'ColorArg0', 'ColorArg1', 'ColorArg2', 'AlphaOp', 'AlphaArg0',
             'AlphaArg1', 'AlphaArg2', 'ResultArg', 'BumpEnvMat00', 'BumpEnvMat01',
             'BumpEnvMat10', 'BumpEnvMat11', 'TexCoordIndex', 'BumpEnvLScale',
             'BumpEnvLOffset', 'TextureTransformFlags', 'Constant']
    shader_constants = ['%sShaderConstant%s' % (kind, suffix)
                        for kind in ('Vertex', 'Pixel')
                        for suffix in ('F', 'B', 'I', '', '1', '2', '3', '4')]
    sampler = ['AddressU', 'AddressV', 'AddressW', 'BorderColor', 'MagFilter', 'MinFilter',
               'MipFilter', 'MipMapLodBias', 'MaxMipLevel', 'MaxAnisotropy', 'SRGBTexture',
               'ElementIndex', 'DMAPOffset']
    light = ['LightAmbient', 'LightAttenuation0', 'LightAttenuation1', 'LightAttenuation2',
             'LightDiffuse', 'LightDirection', 'LightFalloff', 'LightPhi', 'LightPosition',
             'LightRange', 'LightSpecular', 'LightTheta', 'LightType']
    sections = [(render, 'render'), (stage, 'texture_stage'), (['NPatchMode'], 'npatch'),
                (['FVF'], 'fvf'),
                (['ProjectionTransform', 'ViewTransform', 'WorldTransform',
                  'TextureTransform'], 'transform'),
                (['MaterialAmbient', 'MaterialDiffuse', 'MaterialEmissive', 'MaterialPower',
                  'MaterialSpecular'], 'material'),
                (light, 'light'), (['LightEnable'], 'light'),
                (['VertexShader', 'PixelShader'], 'shader'),
                (shader_constants, 'shader_constant'), (['Texture'], 'texture'),
                (sampler, 'sampler_state'), (['Sampler'], 'sampler')]
    table, operation = {}, 0
    for names, category in sections:
        for name in names:
            table[operation] = (name, category)
            operation += 1
    for expected, name in ((48, 'Lighting'), (99, 'SeparateAlphaBlendEnable'),
                           (VERTEX_SHADER_STATE, 'VertexShader'),
                           (PIXEL_SHADER_STATE, 'PixelShader'), (164, 'Texture'),
                           (174, 'MaxAnisotropy')):
        assert table[expected][0] == name, (expected, table[expected])
    return table


STATE_TABLE = _build_state_table()
SAMPLER_PARAMETER_TYPES = frozenset((10, 11, 12, 13, 14))  # SAMPLER, 1D, 2D, 3D, CUBE
SHADER_PARAMETER_TYPES = frozenset((15, 16))               # PIXELSHADER, VERTEXSHADER
SAMPLER_REGISTER_SET = 3                                   # D3DXRS_SAMPLER
TEXTURE_STATE = 164
CATEGORY_CLASS = {'render': 'render', 'sampler_state': 'sampler', 'texture': 'texture',
                  'shader': 'shader'}
CONSTANT_KINDS = frozenset(('literal', 'shader_blob', 'null_shader'))
PASS_CLASSES = ('render_const', 'render_param', 'sampler_const', 'sampler_param',
                'texture_const', 'texture_param', 'shader', 'other_const', 'other_param',
                'unknown')


def _state_kind(data, container, key, operation):
    """How the value of one state record is produced: `(kind, source name)`.

    `literal` is a constant in the container; `shader_blob` a compiled program;
    `expression` a compiled FXLC preshader (usage 0 on a non-shader state);
    `parameter` a reference to a named parameter (usage 1); `array_selector`
    an indexed parameter reference (usage 2). Only the last carries a name.
    """
    resource = container['resources'].get(key)
    category = STATE_TABLE.get(operation, (None, 'unknown'))[1]
    if resource is None:
        return ('null_shader' if category == 'shader' else 'literal'), None
    usage, start, size = resource
    if usage == 0:
        return ('shader_blob' if category == 'shader' else 'expression'), None
    name = data[start:start + size].split(b'\0', 1)[0].decode('latin-1')
    if usage == 1:
        return 'parameter', name
    if usage == 2:
        return 'array_selector', name
    return 'usage_%d' % usage, None


def _sampler_state_blocks(data, container, parameter):
    """The nested state records of one sampler parameter, per element.

    A sampler parameter's value is `u32 state_count` followed by the same
    four-dword state records a pass carries; an array repeats that per element.
    """
    base, at = container['base'], container['base'] + parameter['value_offset']
    blocks = []
    for _ in range(max(parameter['element_count'], 1)):
        (count,) = _u32(data, at)
        at += 4
        states = []
        for _ in range(count):
            states.append(_u32(data, at, 4))
            at += 16
        if at > len(data):
            raise MalformedEffect('sampler state block exceeds effect')
        blocks.append(states)
    del base
    return blocks


def _pass_samplers(data, container, pass_shaders):
    """The sampler parameters the pass's shaders bind, by constant-table name.

    D3DX applies a sampler parameter's state block inside `BeginPass` for every
    sampler register the pass's programs declare, so the pass's sampler and
    texture work is exactly the union of those blocks.
    """
    by_name = {p['name']: p for p in container['parameters']
               if p['type'] in SAMPLER_PARAMETER_TYPES and p['name']}
    found, unresolved = [], 0
    for code in pass_shaders:
        try:
            constants = parse_ctab(code)
        except ValueError:
            unresolved += 1
            continue
        for constant in constants:
            if constant['register_set'] != SAMPLER_REGISTER_SET:
                continue
            parameter = by_name.get(constant['name'])
            if parameter is None:
                unresolved += 1
                continue
            if parameter['index'] not in [p['index'] for p in found]:
                found.append(parameter)
    return found, unresolved


def classify_effect(data, detail=False):
    """Classify the state assignments D3DX applies for every pass of an effect.

    Per pass the counts split by class (render state, sampler state, texture
    binding, shader binding, anything else) and by value source (constant in
    the container, or parameter-driven: a parameter reference, an array
    selector or an FXLC expression). `replayable` is true when every assignment
    of the pass is either constant or a parameter reference D3DX resolves from
    a value the wrapper would own -- no expression, no unknown operation and no
    state outside the four classes.
    """
    container = parse_container(data)
    passes = []
    for technique_index, (technique, technique_passes) in enumerate(container['techniques']):
        for pass_index, (pass_name, states) in enumerate(technique_passes):
            counts = dict.fromkeys(PASS_CLASSES, 0)
            kinds = Counter()
            entries, shaders = [], []
            for state_index, (operation, index, _, _) in enumerate(states):
                name, category = STATE_TABLE.get(operation, ('op_%d' % operation, 'unknown'))
                key = (technique_index, pass_index, 0xffffffff, state_index)
                if key not in container['resources']:
                    key = (technique_index, pass_index, 0, state_index)
                kind, source = _state_kind(data, container, key, operation)
                if kind == 'shader_blob':
                    usage, start, size = container['resources'][key]
                    words = struct.unpack('<%dI' % (size // 4),
                                          data[start:start + size // 4 * 4])
                    end = shader_end(words, 0) if words else None
                    if end is not None:
                        shaders.append(data[start:start + 4 * end])
                counts[_bucket(category, kind)] += 1
                kinds[kind] += 1
                if detail:
                    entries.append({'scope': 'pass', 'state': state_index, 'operation': operation,
                                    'name': name, 'index': index, 'category': category,
                                    'kind': kind, 'source': source})
            samplers, unresolved = _pass_samplers(data, container, shaders)
            counts['unknown'] += unresolved
            kinds['unresolved_sampler'] += unresolved
            for parameter in samplers:
                for element, block in enumerate(_sampler_state_blocks(data, container, parameter)):
                    for state_index, (operation, index, _, _) in enumerate(block):
                        name, category = STATE_TABLE.get(operation,
                                                         ('op_%d' % operation, 'unknown'))
                        key = (0xffffffff, parameter['index'], element, state_index)
                        kind, source = _state_kind(data, container, key, operation)
                        counts[_bucket(category, kind)] += 1
                        kinds[kind] += 1
                        if detail:
                            entries.append({'scope': 'sampler:%s[%d]' % (parameter['name'], element),
                                            'state': state_index, 'operation': operation,
                                            'name': name, 'index': index, 'category': category,
                                            'kind': kind, 'source': source})
            total = sum(counts.values())
            constant = (counts['render_const'] + counts['sampler_const'] +
                        counts['texture_const'] + counts['shader'] + counts['other_const'])
            record = {'technique_index': technique_index, 'technique': technique,
                      'pass_index': pass_index, 'pass': pass_name,
                      'samplers': len(samplers), 'states': total, 'constant': constant,
                      'parameter_driven': total - constant,
                      'constant_fraction': round(constant / total, 4) if total else 0.0,
                      'replayable': (counts['unknown'] == 0 and counts['other_const'] == 0 and
                                     counts['other_param'] == 0 and
                                     kinds['expression'] == 0 and
                                     kinds['array_selector'] == 0),
                      'counts': counts, 'kinds': dict(kinds)}
            if detail:
                record['entries'] = entries
            passes.append(record)
    return passes


def _bucket(category, kind):
    """The reported class of one assignment from its state category and source."""
    reported = CATEGORY_CLASS.get(category, 'other')
    if reported == 'shader':
        return 'shader' if kind in CONSTANT_KINDS else 'other_param'
    if kind == 'unknown' or category == 'unknown' or kind.startswith('usage_'):
        return 'unknown'
    return reported + ('_const' if kind in CONSTANT_KINDS else '_param')


def _archive_effects(game, resolve=True):
    """Yield `(catalogue, path, bytes)` for the `shader/**/*.fb` archive entries.

    With `resolve` (the default) a virtual path present in several catalogues
    yields only the entry the game loads: catalogues are read in ascending
    order and the addon ones last, so the final occurrence wins.
    """
    catalogues = sorted([*game.glob('[0-9][0-9].cat'), *game.glob('addon/[0-9][0-9].cat')])
    chosen = {}
    for cat in catalogues:
        for entry in read_catalogue(cat):
            if entry['path'].startswith('shader/') and entry['path'].endswith('.fb'):
                chosen.setdefault(entry['path'], []).append((cat, entry))
    for path in sorted(chosen):
        for cat, entry in (chosen[path][-1:] if resolve else chosen[path]):
            with cat.with_suffix('.dat').open('rb') as stream:
                stream.seek(entry['offset'])
                yield (str(cat.relative_to(game)), path,
                       bytes(value ^ 0x33 for value in stream.read(entry['size'])))


def archive_classification(game):
    """Classification totals over every installed compiled effect: counts only."""
    totals, kinds = Counter(), Counter()
    digests, groups = set(), {}
    effect_count, pass_count, replayable, signatures = 0, 0, 0, Counter()
    worst = []
    for _, path, data in _archive_effects(game):
        effect_count += 1
        effect = Counter()
        digest = hashlib.sha256(data).hexdigest()
        passes = classify_effect(data)
        for record in passes:
            pass_count += 1
            replayable += bool(record['replayable'])
            kinds.update(record['kinds'])
            effect.update(record['counts'])
            effect['states'] += record['states']
            effect['constant'] += record['constant']
            effect['samplers'] += record['samplers']
            signatures[tuple(record['counts'][key] for key in PASS_CLASSES)] += 1
            worst.append((record['parameter_driven'], record['states'], path,
                          record['technique'], record['pass'], digest))
        totals.update(effect)
        digests.add(digest)
        group = groups.setdefault(str(Path(path).parent), Counter())
        group.update(effect)
        group['effects'] += 1
        group['passes'] += len(passes)
    worst.sort(key=lambda item: (-item[0], item[2], item[3], item[4]))
    seen, distinct_worst = set(), []
    for item in worst:  # identical containers repeat across the hue/light variant trees
        if (item[5], item[3], item[4]) in seen:
            continue
        seen.add((item[5], item[3], item[4]))
        distinct_worst.append(item)
    states = totals['states']
    return {'schema': 1,
            'scope': 'Per-pass classification of the state assignments D3DX applies for the '
                     'installed compiled effects: counts, names and hashes only; no state '
                     'values, parameter values or shader words.',
            'effect_count': effect_count, 'distinct_effect_count': len(digests),
            'pass_count': pass_count,
            'replayable_pass_count': replayable,
            'totals': {'states': states, 'constant': totals['constant'],
                       'parameter_driven': states - totals['constant'],
                       'constant_fraction': round(totals['constant'] / states, 4) if states else 0.0,
                       'samplers': totals['samplers'],
                       'counts': {key: totals[key] for key in PASS_CLASSES},
                       'kinds': dict(sorted(kinds.items()))},
            'per_pass_mean': {key: round(totals[key] / pass_count, 3) for key in
                              ('states', 'constant', 'samplers')} if pass_count else {},
            'pass_signatures': [{'counts': dict(zip(PASS_CLASSES, signature)), 'passes': count}
                                for signature, count in signatures.most_common(12)],
            'top_parameter_driven': [{'path': item[2], 'technique': item[3], 'pass': item[4],
                                      'parameter_driven': item[0], 'states': item[1]}
                                     for item in distinct_worst[:10]],
            'directories': [{'directory': directory, 'effects': group['effects'],
                             'passes': group['passes'], 'states': group['states'],
                             'constant': group['constant'],
                             'parameter_driven': group['states'] - group['constant'],
                             'constant_fraction': (round(group['constant'] / group['states'], 4)
                                                   if group['states'] else 0.0),
                             'counts': {key: group[key] for key in PASS_CLASSES}}
                            for directory, group in sorted(groups.items())]}


def print_pass_detail(game, selector):
    """Print the classified assignments of one `<effect>:<technique>:<pass>`."""
    try:
        effect_key, technique_key, pass_key = selector.split(':')
    except ValueError:
        raise SystemExit('--pass takes <effect>:<technique>:<pass>')
    for catalogue, path, data in _archive_effects(game):
        if effect_key not in (path, Path(path).stem) and not path.endswith(effect_key):
            continue
        for record in classify_effect(data, detail=True):
            if technique_key not in (record['technique'], str(record['technique_index'])):
                continue
            if pass_key not in (record['pass'], str(record['pass_index'])):
                continue
            print('%s %s  %s / %s  states=%d constant=%d parameter_driven=%d '
                  'samplers=%d replayable=%s' %
                  (catalogue, path, record['technique'], record['pass'], record['states'],
                   record['constant'], record['parameter_driven'], record['samplers'],
                   record['replayable']))
            for entry in record['entries']:
                print('  %-28s %-24s op=%-3d index=%-2d %-14s %-14s %s' %
                      (entry['scope'], entry['name'], entry['operation'], entry['index'],
                       entry['category'], entry['kind'], entry['source'] or ''))
            for key in PASS_CLASSES:
                if record['counts'][key]:
                    print('  count %-14s %d' % (key, record['counts'][key]))
            return
    raise SystemExit('no pass matched %r' % selector)


def archive_passes(game):
    """Every pass of every installed numbered-archive effect, derived facts only."""
    passes, effects = [], 0
    for cat in sorted([*game.glob('[0-9][0-9].cat'), *game.glob('addon/[0-9][0-9].cat')]):
        entries = [e for e in read_catalogue(cat)
                   if e['path'].startswith('shader/') and e['path'].endswith('.fb')]
        if not entries:
            continue
        catalogue = str(cat.relative_to(game))
        with cat.with_suffix('.dat').open('rb') as stream:
            for entry in entries:
                stream.seek(entry['offset'])
                data = bytes(value ^ 0x33 for value in stream.read(entry['size']))
                effects += 1
                digest = hashlib.sha256(data).hexdigest()
                for item in parse_effect(data):
                    record = {'catalogue': catalogue, 'path': entry['path'], 'effect_sha256': digest,
                              'technique_index': item['technique_index'], 'technique': item['technique'],
                              'pass_index': item['pass_index'], 'pass': item['pass'],
                              'vs': None, 'ps': None, 'vs_dwords': None, 'ps_dwords': None,
                              'vs_status': 'absent', 'ps_status': 'absent'}
                    for _, stage, fingerprint, dwords, status in item['states']:
                        if record[stage + '_status'] != 'absent':
                            raise MalformedEffect('two %s states in one pass: %s' % (stage, entry['path']))
                        record[stage] = fingerprint
                        record[stage + '_dwords'] = dwords
                        record[stage + '_status'] = status
                    passes.append(record)
    return {'schema': 1,
            'scope': 'Technique/pass shader pairings of the installed compiled effects; '
                     'derived names, hashes and lengths only.',
            'effect_count': effects, 'pass_count': len(passes),
            'status_counts': dict(sorted(Counter('%s:%s' % (stage, p[stage + '_status'])
                                                 for p in passes for stage in ('vs', 'ps')).items())),
            'passes': passes}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('game', type=Path)
    parser.add_argument('--output', type=Path,
                        help='write the technique/pass shader pairing table here')
    parser.add_argument('--classification', type=Path,
                        help='write the per-pass state classification summary here')
    parser.add_argument('--pass', dest='pass_selector', metavar='EFFECT:TECHNIQUE:PASS',
                        help='print the classified assignments of one pass')
    arguments = parser.parse_args()
    if not (arguments.output or arguments.classification or arguments.pass_selector):
        parser.error('one of --output, --classification or --pass is required')
    if arguments.output:
        result = archive_passes(arguments.game)
        arguments.output.write_text(json.dumps(result, indent=1) + '\n')
        print(json.dumps({key: value for key, value in result.items() if key != 'passes'},
                         indent=2))
    if arguments.classification:
        summary = archive_classification(arguments.game)
        arguments.classification.write_text(json.dumps(summary, indent=1) + '\n')
        print(json.dumps({key: value for key, value in summary.items()
                          if key not in ('directories', 'pass_signatures')}, indent=2))
    if arguments.pass_selector:
        print_pass_detail(arguments.game, arguments.pass_selector)


if __name__ == '__main__':
    main()
